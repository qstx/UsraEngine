"""Shared standard-library helpers for build, capture and acceptance automation."""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


ENGINE = Path(__file__).resolve().parents[1]


def expect(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha256(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest().upper()


def read_json(path):
    for attempt in range(50):
        try:
            if os.name == 'nt':
                from WindowsAutomation import shared_text_file
                with shared_text_file(path) as stream:
                    return json.load(stream)
            return json.loads(Path(path).read_text(encoding='utf-8-sig'))
        except (OSError, json.JSONDecodeError):
            if attempt == 49:
                raise
            time.sleep(.02)


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    for attempt in range(50):
        try:
            if os.name == 'nt':
                from WindowsAutomation import replace_file
                replace_file(temporary, path)
            else:
                os.replace(temporary, path)
            return
        except PermissionError:
            if attempt == 49:
                raise
            time.sleep(.02)


def inside(path, root):
    path, root = Path(path).resolve(), Path(root).resolve()
    if path == root or not path.is_relative_to(root):
        raise RuntimeError(f'Refusing filesystem mutation outside {root}: {path}')
    return path


def remove_tree(path, root):
    path = inside(path, root)
    if path.exists():
        shutil.rmtree(path)


@contextlib.contextmanager
def environment(**values):
    previous = {key: os.environ.get(key) for key in values}
    try:
        for key, value in values.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = str(value)
        yield
    finally:
        for key, value in previous.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value


def hidden_options():
    """Suppress helper consoles without overriding a GUI application's window state."""
    if os.name != 'nt':
        return {}
    return {'creationflags': subprocess.CREATE_NO_WINDOW}


def run(command, *, expected=0, timeout=None, cwd=None, env=None):
    env = dict(os.environ if env is None else env)
    env.setdefault('PYTHONIOENCODING', 'utf-8')
    scratch = ENGINE / 'Build/Temp'
    scratch.mkdir(parents=True, exist_ok=True)
    # Compiler servers can inherit output handles. Wait for the command itself,
    # rather than waiting for all descendants to close a captured pipe.
    with tempfile.TemporaryFile(dir=scratch) as stdout, tempfile.TemporaryFile(dir=scratch) as stderr:
        with subprocess.Popen(list(map(str, command)), stdout=stdout, stderr=stderr,
                              cwd=cwd, env=env, **hidden_options()) as child:
            try:
                status = child.wait(timeout=timeout)
            except BaseException:
                child.kill(); child.wait()
                raise
        stdout.seek(0); stderr.seek(0)
        text = (stdout.read() + stderr.read()).decode('utf-8', errors='replace')
    if status != expected:
        raise RuntimeError(f'Unexpected exit {status} (expected {expected}): {command}\n{text}')
    return text


def run_logged(command, output, errors, *, timeout=180, env=None):
    output, errors = Path(output), Path(errors)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('wb') as stdout, errors.open('wb') as stderr:
        with subprocess.Popen(list(map(str, command)), stdout=stdout, stderr=stderr,
                              env=env, **hidden_options()) as child:
            try:
                status = child.wait(timeout=timeout)
            except BaseException:
                child.kill()
                child.wait()
                raise
    expect(status == 0, f'Command exited {status}: {command}; inspect {errors}')


def wait_until(check, seconds, description, interval=.15):
    deadline = time.monotonic() + seconds
    while True:
        value = check()
        if value:
            return value
        if time.monotonic() >= deadline:
            raise RuntimeError(f'Timed out: {description}')
        time.sleep(interval)
