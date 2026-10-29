"""Locate and validate the supported VS2026/v145 toolchain."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from WorkspaceCommon import full_path, main_entry


def get_toolchain(cmake='cmake', build_directory=None):
    command = shutil.which(str(cmake))
    if not command:
        raise RuntimeError(f'CMake was not found: {cmake}')
    output = subprocess.check_output([command, '--version'], text=True)
    match = re.search(r'cmake version (\d+)\.(\d+)\.(\d+)', output)
    if not match:
        raise RuntimeError(f'Cannot inspect CMake: {command}')
    version = match.group(0)
    if tuple(map(int, match.groups())) < (4, 4, 5):
        raise RuntimeError(f'CMake 4.4.5 or newer is required; found {version} at {command}')
    if build_directory:
        cache_path = full_path(build_directory) / 'CMakeCache.txt'
        if cache_path.is_file():
            cache = cache_path.read_text(encoding='utf-8-sig')
            generator = re.search(r'^CMAKE_GENERATOR:INTERNAL=(.*)$', cache, re.M)
            toolset = re.search(r'^CMAKE_GENERATOR_TOOLSET:INTERNAL=(.*)$', cache, re.M)
            if (not generator or generator[1] != 'Visual Studio 18 2026'
                    or (toolset and toolset[1] and not re.match(r'^v145(?:,|$)', toolset[1]))):
                raise RuntimeError(f'Incompatible build cache at {cache_path}. Choose a new build directory '
                                   'for VS2026/v145, or explicitly clean the old cache. No files were removed.')
    locator = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    if not locator.is_file():
        raise RuntimeError('VS2026 with Desktop development with C++ is required (vswhere is missing).')
    installation = subprocess.check_output([
        str(locator), '-utf8', '-latest', '-version', '[18.0,19.0)', '-products', '*',
        '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
    ], text=True, encoding='utf-8-sig').strip()
    if not installation:
        raise RuntimeError('VS2026 with Desktop development with C++ and the v145 tools is required.')
    installation = installation.splitlines()[0]
    tools_version = Path(installation) / 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'
    if not tools_version.is_file() or not re.match(r'^14\.5\d\.', tools_version.read_text(encoding='utf-8-sig').strip()):
        raise RuntimeError(f'VS2026 v145 C++ tools are missing at {installation}')
    return dict(CMake=command, Version=version, VisualStudio=installation,
                Generator='Visual Studio 18 2026', Toolset='v145')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--build-dir')
    args = parser.parse_args()
    print(json.dumps(get_toolchain(args.cmake, args.build_dir)))


if __name__ == '__main__':
    main_entry(main)
