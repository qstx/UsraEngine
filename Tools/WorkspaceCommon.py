"""Standard-library helpers shared by the Windows workspace commands."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


def full_path(value):
    return Path(os.path.abspath(value))


def write_changed(path, text):
    path = Path(path)
    text = text.replace('\r\n', '\n')
    data = text.encode('utf-8')
    if path.is_file() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def run(command):
    subprocess.run([str(arg) for arg in command], check=True)


def main_entry(main):
    try:
        main()
    except subprocess.CalledProcessError as error:
        print(f'Command failed ({error.returncode}): {error.cmd}', file=sys.stderr)
        sys.exit(error.returncode if 0 < error.returncode < 256 else 1)
    except (OSError, ValueError, RuntimeError, ET.ParseError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
