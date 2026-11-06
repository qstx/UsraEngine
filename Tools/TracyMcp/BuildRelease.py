"""Build/package pinned Tracy MCP bindings and a verified shared CPython runtime."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import uuid
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from AutomationSupport import expect, inside, read_json, remove_tree, run, sha256, write_json
from WindowsToolchain import get_toolchain
from WorkspaceCommon import main_entry


PYTHON_VERSION, DISTRIBUTION = '3.11.17', '20261009'
RUNTIME_SHA256 = 'c3045115465bb4aa7d1ca6bfe6ce311ba157d726d768bbb3520fad2580831c3f'


def safe_extract(archive, destination):
    destination = destination.resolve()
    with tarfile.open(archive) as stream:
        for member in stream.getmembers():
            inside(destination / member.name, destination)
            expect(not member.issym() and not member.islnk() and (member.isfile() or member.isdir()), f'Unsupported archive member: {member.name}')
        stream.extractall(destination)


def publish(staging, release, backup):
    moved = False
    try:
        if release.exists():
            release.rename(backup)
            moved = True
        staging.rename(release)
    except BaseException:
        if moved and not release.exists():
            backup.rename(release)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--python-executable', type=Path, default=Path(sys.executable))
    parser.add_argument('--workspace-root', type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument('--package-existing-binding', action='store_true')
    parser.add_argument('--cmake', default='cmake')
    args = parser.parse_args()
    engine = args.workspace_root.resolve() / 'Engine'
    tracy, build = engine / 'ThirdParty/tracy', engine / 'Build/TracyMcp'
    cmake_build = build / 'CMakeBuildVS2026'
    version_header = tracy / 'public/common/TracyVersion.hpp'
    expect(version_header.is_file(), 'Initialize Engine/ThirdParty/tracy before building.')
    python = args.python_executable.resolve()
    info = run([python, '-c', 'import sys,struct; print(*sys.version_info[:3],struct.calcsize("P")*8)']).split()
    expect(len(info) == 4 and info[:2] == ['3', '11'] and info[3] == '64', f'Tracy MCP release requires Python 3.11 x64; found {info}')
    text = version_header.read_text(encoding='utf-8-sig')
    parts = [re.search(rf'constexpr int {part} = (\d+);', text) for part in ('Major', 'Minor', 'Patch')]
    expect(all(parts), f'Cannot read Tracy version: {version_header}')
    version = '.'.join(match[1] for match in parts)
    release = Path(__file__).parent / 'Release/WindowsX64' / version
    release.parent.mkdir(parents=True, exist_ok=True)
    build.mkdir(parents=True, exist_ok=True)
    staging, backup = [build / (name + uuid.uuid4().hex) for name in ('Staging-', 'Previous-')]
    archive_name = f'cpython-{PYTHON_VERSION}+{DISTRIBUTION}-x86_64-pc-windows-msvc-install_only_stripped.tar.gz'
    archive = build / archive_name
    url = f'https://github.com/astral-sh/python-build-standalone/releases/download/{DISTRIBUTION}/{archive_name.replace("+", "%2B")}'
    if not archive.exists():
        print(f'Downloading portable CPython {PYTHON_VERSION}...')
        urllib.request.urlretrieve(url, archive)
    expect(sha256(archive).lower() == RUNTIME_SHA256, 'Portable Python archive checksum mismatch.')
    extraction = build / f'PythonRuntime-{PYTHON_VERSION}-{DISTRIBUTION}'
    runtime = extraction / 'python'
    if not (runtime / 'python.exe').is_file():
        extraction.mkdir(parents=True, exist_ok=True)
        safe_extract(archive, extraction)
    expect((runtime / 'python.exe').is_file(), 'Portable Python runtime extraction failed.')
    binding_directory = cmake_build / 'python/Release'
    if not args.package_existing_binding:
        toolchain = get_toolchain(args.cmake, cmake_build)
        print(run([toolchain['CMake'], '-S', tracy, '-B', cmake_build, '-G', toolchain['Generator'], '-A', 'x64', '-T', 'v145',
                   f'-DCMAKE_GENERATOR_INSTANCE={toolchain["VisualStudio"]}', '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
                   '-DTRACY_CLIENT_PYTHON=ON', '-DTRACY_STATIC=OFF', '-DBUILD_SHARED_LIBS=OFF', '-DNO_LTO=ON',
                   '-DCPM_SOURCE_CACHE:PATH=OFF', '-DCMAKE_CXX_FLAGS=/utf-8 /EHsc /D_CRT_SECURE_NO_WARNINGS /wd4018', f'-DPython_EXECUTABLE={python}']))
        print(run([toolchain['CMake'], '--build', cmake_build, '--config', 'Release', '--target', 'TracyServerBindings', '--parallel', '2']))
    elif not binding_directory.is_dir():
        binding_directory = build / 'python/Release'
    bindings = list(binding_directory.glob('TracyServerBindings*.pyd'))
    expect(len(bindings) == 1, f'Expected one TracyServerBindings extension in {binding_directory}; found {len(bindings)}')
    binding = bindings[0]
    revision = run(['git', '-C', tracy, 'rev-parse', 'HEAD']).strip()
    marker_path = build / 'TracyServerBindings-build.json'
    marker = dict(tracyVersion=version, tracyRevision=revision, pythonAbi='cp311-win_amd64', sha256=sha256(binding).lower())
    if args.package_existing_binding:
        expect(marker_path.is_file(), 'Cannot reuse a binding without build provenance; build it once first.')
        previous = read_json(marker_path)
        expect(all(previous[key] == marker[key] for key in ('tracyRevision', 'pythonAbi', 'sha256')), 'Existing binding provenance does not match revision, ABI or binary contents.')
    else:
        write_json(marker_path, marker)
    sources = ('extra/mcp/tracy_mcp.py', 'extra/mcp/eval_guide.md', 'extra/mcp/requirements.txt',
               'profiler/src/llm/system.prompt.md', 'public/common/TracyProtocol.hpp', 'LICENSE')
    for relative in sources:
        expect((tracy / relative).is_file(), f'Required Tracy MCP input is missing: {relative}')
    staging.mkdir()
    for relative in sources:
        target = staging / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(tracy / relative, target)
    shutil.copy2(Path(__file__).with_name('Start-TracyMcp.py'), staging)
    (staging / 'Start-TracyMcp.bat').write_text(f'@echo off\nsetlocal DisableDelayedExpansion\nset "PYTHONUTF8=1"\n"%~dp0..\\..\\..\\..\\Python\\{PYTHON_VERSION}\\python.exe" "%~dp0Start-TracyMcp.py" %*\nexit /b %errorlevel%\n', encoding='utf-8')
    for path in [binding, *binding_directory.glob('*.dll')]:
        shutil.copy2(path, staging)
    packages = staging / 'PythonPackages'
    runtime_exe = runtime / 'python.exe'
    print(run([runtime_exe, '-m', 'pip', 'install', '--disable-pip-version-check', '--no-warn-script-location', '--target', packages, '-r', staging / 'extra/mcp/requirements.txt']))
    (staging / 'sitecustomize.py').write_text('import site\nfrom pathlib import Path\nsite.addsitedir(str(Path(__file__).resolve().parent / "PythonPackages"))\n', encoding='utf-8')
    env = dict(os.environ, PYTHONPATH=str(staging), PYTHONDONTWRITEBYTECODE='1')
    run([runtime_exe, '-c', 'import mcp, TracyServerBindings'], env=env)
    lock = run([runtime_exe, '-m', 'pip', 'freeze', '--path', packages])
    (staging / 'requirements.lock.txt').write_text(lock, encoding='utf-8')
    for relative in ('pythonwin', 'win32com', 'win32comext', 'adodbapi', 'isapi', 'bin'):
        remove_tree(packages / relative, staging)
    for path in list(staging.rglob('__pycache__')):
        remove_tree(path, staging)
    run([runtime_exe, '-c', 'import mcp, TracyServerBindings'], env=env)
    shared = engine / 'Tools/Python' / PYTHON_VERSION
    if shared.exists():
        expect((shared / 'python.exe').is_file() and (shared / 'runtime-manifest.json').is_file(), f'Shared runtime is incomplete: {shared}')
        expect(read_json(shared / 'runtime-manifest.json')['archiveSha256'] == RUNTIME_SHA256, 'Shared runtime does not match pinned distribution')
    else:
        stage = build / ('PythonStage-' + uuid.uuid4().hex)
        shutil.copytree(runtime, stage)
        write_json(stage / 'runtime-manifest.json', dict(version=PYTHON_VERSION, architecture='x64', distribution=f'astral-sh/python-build-standalone@{DISTRIBUTION}',
                   archive=archive_name, archiveUrl=url, archiveSha256=RUNTIME_SHA256))
        expect(run([stage / 'python.exe', '-c', 'import sys; print(".".join(map(str,sys.version_info[:3])))']).strip() == PYTHON_VERSION, 'Portable runtime version mismatch')
        shared.parent.mkdir(parents=True, exist_ok=True)
        stage.rename(shared)
    readme = Path(__file__).with_name('README.md').read_text(encoding='utf-8-sig').replace('@TRACY_VERSION@', version).replace('@TRACY_REVISION@', revision)
    (staging / 'README.md').write_text(readme, encoding='utf-8')
    (staging / '.gitignore').write_text('extra/mcp/tracy_mcp.pid\nextra/mcp/tracy_mcp.port\nextra/mcp/tracy_mcp.crash.log\n__pycache__/\nCaptures/\n', encoding='utf-8')
    write_json(staging / 'release-manifest.json', dict(package=f'TracyMcp-WindowsX64-{version}', tracyVersion=version, tracyRevision=revision,
               configuration='Release', architecture='x64', buildPythonVersion='.'.join(info[:3]), runtimePythonVersion=PYTHON_VERSION,
               pythonRuntimePath=f'Engine/Tools/Python/{PYTHON_VERSION}', pythonDistribution=f'astral-sh/python-build-standalone@{DISTRIBUTION}',
               pythonRuntimeArchive=archive_name, pythonRuntimeUrl=url, pythonRuntimeSha256=RUNTIME_SHA256, pythonAbi='cp311-win_amd64',
               buildMode='reused-existing-release-artifact' if args.package_existing_binding else 'built-from-pinned-submodule',
               cmakeOptions=['TRACY_CLIENT_PYTHON=ON', 'TRACY_STATIC=OFF', 'BUILD_SHARED_LIBS=OFF', 'NO_LTO=ON',
                             'CMAKE_CXX_FLAGS=/utf-8 /EHsc /D_CRT_SECURE_NO_WARNINGS /wd4018']))
    inside(staging, build); inside(backup, build); inside(release, Path(__file__).parent / 'Release')
    publish(staging, release, backup)
    print(f'Tracy MCP release created at {release}\nShared Python runtime: {shared}')


if __name__ == '__main__':
    main_entry(main)
