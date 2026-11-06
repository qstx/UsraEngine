"""Generate the multi-size Editor ICO using Windows GDI+ (no Python packages required)."""
import argparse
import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
import struct
import tempfile
import uuid
from AutomationSupport import ENGINE, expect
from WorkspaceCommon import main_entry


def generate(source, output):
    expect(hasattr(C, 'WinDLL'), 'Icon generation requires Windows GDI+.')
    gdip = C.WinDLL('gdiplus')
    class Startup(C.Structure):
        _fields_ = [('version', W.UINT), ('callback', C.c_void_p), ('suppress_thread', W.BOOL), ('suppress_codecs', W.BOOL)]
    def bind(name, types):
        function = getattr(gdip, name)
        function.argtypes, function.restype = types, C.c_int
        def invoke(*values):
            status = function(*values)
            expect(status == 0, f'{name} failed with GDI+ status {status}')
        return invoke
    ptr = C.c_void_p
    startup = bind('GdiplusStartup', [C.POINTER(C.c_size_t), C.POINTER(Startup), ptr])
    load = bind('GdipLoadImageFromFile', [W.LPCWSTR, C.POINTER(ptr)])
    create = bind('GdipCreateBitmapFromScan0', [C.c_int, C.c_int, C.c_int, C.c_int, ptr, C.POINTER(ptr)])
    graphics = bind('GdipGetImageGraphicsContext', [ptr, C.POINTER(ptr)])
    draw = bind('GdipDrawImageRectRectI', [ptr, ptr] + [C.c_int] * 9 + [ptr, ptr, ptr])
    save = bind('GdipSaveImageToFile', [ptr, W.LPCWSTR, ptr, ptr])
    dispose = bind('GdipDisposeImage', [ptr])
    delete = bind('GdipDeleteGraphics', [ptr])
    width = bind('GdipGetImageWidth', [ptr, C.POINTER(W.UINT)])
    height = bind('GdipGetImageHeight', [ptr, C.POINTER(W.UINT)])
    token, image = C.c_size_t(), ptr()
    startup(C.byref(token), C.byref(Startup(1, None, False, False)), None)
    sizes, images = (16, 24, 32, 48, 64, 128, 256), []
    encoder = (C.c_ubyte * 16).from_buffer_copy(uuid.UUID('557cf406-1a04-11d3-9a73-0000f81ef32e').bytes_le)
    try:
        load(str(source.resolve()), C.byref(image))
        w, h = W.UINT(), W.UINT()
        width(image, C.byref(w)); height(image, C.byref(h))
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='UrsaIcon-', dir=output.parent) as temporary:
            for size in sizes:
                bitmap, context = ptr(), ptr()
                create(size, size, 0, 0x26200A, None, C.byref(bitmap))
                try:
                    graphics(bitmap, C.byref(context))
                    bind('GdipSetCompositingMode', [ptr, C.c_int])(context, 1)
                    bind('GdipSetCompositingQuality', [ptr, C.c_int])(context, 2)
                    bind('GdipSetInterpolationMode', [ptr, C.c_int])(context, 7)
                    bind('GdipSetPixelOffsetMode', [ptr, C.c_int])(context, 2)
                    draw(context, image, 0, 0, size, size, 0, 0, w.value, h.value, 2, None, None, None)
                    png = Path(temporary) / f'{size}.png'
                    save(bitmap, str(png), encoder, None)
                    images.append(png.read_bytes())
                finally:
                    if context.value: delete(context)
                    dispose(bitmap)
    finally:
        if image.value: dispose(image)
        gdip.GdiplusShutdown.argtypes = [C.c_size_t]
        gdip.GdiplusShutdown(token)
    offset = 6 + 16 * len(sizes)
    header = bytearray(struct.pack('<HHH', 0, 1, len(sizes)))
    for size, data in zip(sizes, images):
        header.extend(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), offset))
        offset += len(data)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + b''.join(images))
    print(f'Generated {output} with sizes {sizes}.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ENGINE / 'Editor/Resources/UrsaEditor.png')
    parser.add_argument('--output', type=Path, default=ENGINE / 'Editor/Resources/UrsaEditor.ico')
    args = parser.parse_args()
    generate(args.source, args.output)


if __name__ == '__main__':
    main_entry(main)
