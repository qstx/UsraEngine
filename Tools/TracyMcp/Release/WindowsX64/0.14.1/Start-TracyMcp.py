"""Start the packaged Tracy MCP server with its verified shared CPython runtime."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys


def start(release, captures, port, check=False):
    manifest_path = release / 'release-manifest.json'
    if not manifest_path.is_file():
        raise RuntimeError(f'Tracy MCP release manifest is missing: {manifest_path}')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    tools = release.parents[3]
    python = tools / 'Python' / manifest['runtimePythonVersion'] / 'python.exe'
    runtime_path = python.parent / 'runtime-manifest.json'
    if not python.is_file() or not runtime_path.is_file():
        raise RuntimeError(f'Shared Python runtime is missing: {python}')
    runtime = json.loads(runtime_path.read_text(encoding='utf-8-sig'))
    if runtime['archiveSha256'] != manifest['pythonRuntimeSha256']:
        raise RuntimeError('The shared Python runtime does not match the Tracy MCP release manifest.')
    version = subprocess.check_output([str(python), '-c', 'import sys,struct; print("%s %s" % (".".join(map(str,sys.version_info[:3])),struct.calcsize("P")*8))'], text=True).strip()
    if version != manifest['runtimePythonVersion'] + ' 64':
        raise RuntimeError(f'Expected Python {manifest["runtimePythonVersion"]} x64; found {version}')
    server = release / 'extra/mcp/tracy_mcp.py'
    if len(list(release.glob('TracyServerBindings*.pyd'))) != 1 or not server.is_file():
        raise RuntimeError(f'Tracy MCP release is incomplete: {release}')
    captures.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PYTHONPATH=str(release) + (os.pathsep + os.environ['PYTHONPATH'] if os.environ.get('PYTHONPATH') else ''),
               PYTHONDONTWRITEBYTECODE='1', TRACY_CAPTURES_DIR=str(captures), TRACY_MCP_PORT=str(port))
    subprocess.run([str(python), '-c', 'import mcp, TracyServerBindings'], check=True, env=env)
    if check:
        print('Tracy MCP runtime, manifest and imports verified.')
        return 0
    print(f'Starting Tracy MCP at http://127.0.0.1:{port}/mcp', flush=True)
    print(f'Capture directory: {captures}', flush=True)
    return subprocess.call([str(python), str(server)], env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release-directory', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--captures-directory', type=Path, default=Path(os.environ.get('LOCALAPPDATA', Path.home())) / 'UrsaEngine/TracyMcp/Captures')
    parser.add_argument('--port', type=int, default=47380)
    parser.add_argument('--check', action='store_true', help='verify runtime and imports without starting the server')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error('--port must be between 1 and 65535')
    try:
        return start(args.release_directory.resolve(), args.captures_directory.resolve(), args.port, args.check)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
