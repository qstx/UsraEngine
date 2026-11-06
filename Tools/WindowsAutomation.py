"""Small Win32 adapters for owned-process cleanup, window tests, file leases and pixels."""
import contextlib
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
from AutomationSupport import expect


def api(library, name, result, *arguments):
    function = getattr(C.WinDLL(library, use_last_error=True), name)
    function.restype, function.argtypes = result, list(arguments)
    return function


class Process:
    def __init__(self, pid):
        self.pid = pid
        self._handle = api('kernel32', 'OpenProcess', W.HANDLE, W.DWORD, W.BOOL, W.DWORD)(0x101411, False, pid)
        if not self._handle:
            error = C.get_last_error()
            if error in (6, 87, 1168):
                raise ProcessLookupError(pid)
            raise C.WinError(error)

    def poll(self):
        return None if api('kernel32', 'WaitForSingleObject', W.DWORD, W.HANDLE, W.DWORD)(self._handle, 0) == 258 else 0

    def close(self):
        if self._handle:
            api('kernel32', 'CloseHandle', W.BOOL, W.HANDLE)(self._handle)
            self._handle = None

    def __del__(self):
        self.close()


def executable(process):
    size = W.DWORD(32768)
    text = C.create_unicode_buffer(size.value)
    expect(api('kernel32', 'QueryFullProcessImageNameW', W.BOOL, W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD))(
        int(process._handle), 0, text, C.byref(size)), f'Cannot inspect process {process.pid}')
    return Path(text.value).resolve()


def find_window(pid, titles=None):
    found = []
    callback_type = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def visit(window, _):
        owner = W.DWORD()
        api('user32', 'GetWindowThreadProcessId', W.DWORD, W.HWND, C.POINTER(W.DWORD))(window, C.byref(owner))
        if owner.value == pid:
            text = C.create_unicode_buffer(256)
            api('user32', 'GetWindowTextW', C.c_int, W.HWND, W.LPWSTR, C.c_int)(window, text, 256)
            if titles is None or text.value in titles:
                found.append(window)
        return True
    callback = callback_type(visit)
    api('user32', 'EnumWindows', W.BOOL, callback_type, W.LPARAM)(callback, 0)
    return found[0] if found else None


def stop_process(process, expected_executable):
    if process is None or process.poll() is not None:
        return
    actual = os.path.normcase(str(executable(process)))
    expect(actual == os.path.normcase(str(Path(expected_executable).resolve())), f'Refusing to close an unrelated process: {process.pid}')
    window = find_window(process.pid)
    if window:
        api('user32', 'PostMessageW', W.BOOL, W.HWND, W.UINT, W.WPARAM, W.LPARAM)(window, 0x10, 0, 0)
    wait = api('kernel32', 'WaitForSingleObject', W.DWORD, W.HANDLE, W.DWORD)
    if wait(int(process._handle), 3000) == 258:
        expect(api('kernel32', 'TerminateProcess', W.BOOL, W.HANDLE, W.UINT)(int(process._handle), 1), f'Cannot close process {process.pid}')
        expect(wait(int(process._handle), 5000) == 0, f'Acceptance process did not close: {process.pid}')
    process.poll()


@contextlib.contextmanager
def shared_text_file(path):
    import msvcrt
    handle = api('kernel32', 'CreateFileW', W.HANDLE, W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE)(
        str(Path(path).resolve()), 0x80000000, 7, None, 3, 0x80, None)
    if handle == C.c_void_p(-1).value:
        raise C.WinError(C.get_last_error())
    try:
        descriptor = msvcrt.open_osfhandle(handle, os.O_RDONLY)
    except BaseException:
        api('kernel32', 'CloseHandle', W.BOOL, W.HANDLE)(handle)
        raise
    with os.fdopen(descriptor, 'r', encoding='utf-8-sig') as stream:
        yield stream


def replace_file(source, target):
    source, target = str(Path(source).resolve()), str(Path(target).resolve())
    if Path(target).exists() and api('kernel32', 'ReplaceFileW', W.BOOL, W.LPCWSTR, W.LPCWSTR, W.LPCWSTR, W.DWORD, C.c_void_p, C.c_void_p)(
            target, source, None, 2, None, None):
        return
    if not api('kernel32', 'MoveFileExW', W.BOOL, W.LPCWSTR, W.LPCWSTR, W.DWORD)(source, target, 9):
        raise C.WinError(C.get_last_error())


@contextlib.contextmanager
def exclusive_file(path, write=False):
    handle = api('kernel32', 'CreateFileW', W.HANDLE, W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE)(
        str(Path(path).resolve()), 0xC0000000 if write else 0x80000000, 0, None, 3, 0x80, None)
    if handle == C.c_void_p(-1).value:
        raise C.WinError(C.get_last_error())
    try:
        yield
    finally:
        api('kernel32', 'CloseHandle', W.BOOL, W.HANDLE)(handle)


def memory(process):
    class Counters(C.Structure):
        _fields_ = [('cb', W.DWORD), ('PageFaultCount', W.DWORD)] + [(name, C.c_size_t) for name in
            ('PeakWorkingSetSize', 'WorkingSetSize', 'QuotaPeakPagedPoolUsage', 'QuotaPagedPoolUsage',
             'QuotaPeakNonPagedPoolUsage', 'QuotaNonPagedPoolUsage', 'PagefileUsage', 'PeakPagefileUsage', 'PrivateUsage')]
    counters = Counters()
    counters.cb = C.sizeof(counters)
    expect(api('psapi', 'GetProcessMemoryInfo', W.BOOL, W.HANDLE, C.c_void_p, W.DWORD)(int(process._handle), C.byref(counters), counters.cb), 'Cannot query process memory')
    return dict(privateBytes=counters.PrivateUsage, workingSetBytes=counters.WorkingSetSize)


class Bitmap:
    def __init__(self, path):
        class Startup(C.Structure):
            _fields_ = [('version', W.UINT), ('callback', C.c_void_p), ('thread', W.BOOL), ('codecs', W.BOOL)]
        self.token, self.image = C.c_size_t(), C.c_void_p()
        status = api('gdiplus', 'GdiplusStartup', C.c_int, C.POINTER(C.c_size_t), C.POINTER(Startup), C.c_void_p)(C.byref(self.token), C.byref(Startup(1, None, False, False)), None)
        expect(status == 0, 'GDI+ startup failed')
        try:
            expect(api('gdiplus', 'GdipCreateBitmapFromFile', C.c_int, W.LPCWSTR, C.POINTER(C.c_void_p))(str(Path(path).resolve()), C.byref(self.image)) == 0, f'Cannot open image: {path}')
            w, h = W.UINT(), W.UINT()
            expect(api('gdiplus', 'GdipGetImageWidth', C.c_int, C.c_void_p, C.POINTER(W.UINT))(self.image, C.byref(w)) == 0, 'Cannot read image width')
            expect(api('gdiplus', 'GdipGetImageHeight', C.c_int, C.c_void_p, C.POINTER(W.UINT))(self.image, C.byref(h)) == 0, 'Cannot read image height')
            self.width, self.height = w.value, h.value
            self._pixel = api('gdiplus', 'GdipBitmapGetPixel', C.c_int, C.c_void_p, C.c_int, C.c_int, C.POINTER(W.DWORD))
        except BaseException:
            self.close()
            raise

    def argb(self, x, y):
        color = W.DWORD()
        expect(self._pixel(self.image, x, y, C.byref(color)) == 0, 'Cannot read image pixel')
        return color.value

    def pixel(self, x, y):
        color = self.argb(x, y)
        return ((color >> 16) & 255, (color >> 8) & 255, color & 255)

    @property
    def size(self):
        return self.width, self.height

    def close(self):
        if self.image.value:
            api('gdiplus', 'GdipDisposeImage', C.c_int, C.c_void_p)(self.image)
            self.image = C.c_void_p()
        if self.token.value:
            api('gdiplus', 'GdiplusShutdown', None, C.c_size_t)(self.token)
            self.token = C.c_size_t()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
