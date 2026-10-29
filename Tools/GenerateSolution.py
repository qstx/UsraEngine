"""Generate the VS2026 C++/C# workspace, including game and Editor scripts.

Requires CMake 4.4.5+, VS2026/v145, .NET 10 and initialized third-party
submodules. Tests are omitted by default. The first run builds workspace
tools; later runs refresh incrementally. No PowerShell is needed.
"""

import argparse
from pathlib import Path
import sys

from CheckProjectStructure import check
from WindowsToolchain import get_toolchain
from WorkspaceCommon import full_path, main_entry, run


def main():
    engine_root = full_path(Path(__file__).parent / '..')
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', default=str(engine_root / 'Build/VS2026'), help='build directory (default: Engine/Build/VS2026)')
    parser.add_argument('--project-dir', default=str(engine_root.parent / 'Demo'), help='game project directory (default: Demo)')
    parser.add_argument('--cmake', default='cmake', help='CMake executable')
    parser.add_argument('--tests', action='store_true', help='include test projects')
    args = parser.parse_args()
    build, project = full_path(args.build_dir), full_path(args.project_dir)
    if not (project / 'Assets').is_dir():
        raise RuntimeError(f'Project Assets directory is missing: {project}')
    if not (engine_root / 'ThirdParty/tracy/CMakeLists.txt').is_file():
        raise RuntimeError(f'Tracy submodule is missing. Run: git -C "{engine_root}" submodule update --init ThirdParty/tracy')
    toolchain = get_toolchain(args.cmake, build)
    print(f'{toolchain["Version"]}; VS2026/v145 at {toolchain["VisualStudio"]}', flush=True)
    configure = [toolchain['CMake'], '-S', engine_root, '-B', build, '-G', toolchain['Generator'], '-A', 'x64', '-T', 'v145',
                 f'-DCMAKE_GENERATOR_INSTANCE={toolchain["VisualStudio"]}', '-DURSA_BUILD_EDITOR=ON', '-DURSA_ENABLE_DOTNET=ON',
                 '-DURSA_ENABLE_TRACY=ON', '-DURSA_MSVC_PARALLEL_COMPILE=ON', f'-DURSA_BUILD_TESTS={"ON" if args.tests else "OFF"}',
                 f'-DURSA_PROJECT_DIR={project.as_posix()}', f'-DURSA_WORKSPACE_PYTHON={Path(sys.executable).as_posix()}']
    print('[1/5] Configure workspace tools', flush=True)
    run(configure + ['-DURSA_WORKSPACE_BOOTSTRAP=ON'])
    print('[2/5] Build workspace tools (no game compilation or content import)', flush=True)
    run([toolchain['CMake'], '--build', build, '--config', 'Debug', '--parallel', '3', '--target', 'UrsaScriptCompile', '--', '/nodeReuse:false'])
    compiler, description = build / 'Debug/UrsaScriptCompile.exe', build / 'UnifiedWorkspace.json'
    ide = [compiler, project, '--ide', '--workspace', description]
    print('[3/5] Collect project scripts and generate C# projects', flush=True)
    run(ide + ['--prepare-workspace'])
    print('[4/5] Prepare IDE framework bindings and both configurations', flush=True)
    for configuration in ('Debug', 'Release'):
        for name in ('Ursa.Editor', 'Ursa.ManagedHost'):
            run(['dotnet', 'build', build / f'Ide/Managed/{name}/{name}.csproj', '--configuration', configuration,
                 '--nologo', '-m:1', '--disable-build-servers', '-nr:false'])
    print('[5/5] Complete the unified solution and publish the association', flush=True)
    run(configure + ['-DURSA_WORKSPACE_BOOTSTRAP=OFF'])
    run(ide)
    check(build)
    print(f'Solution ready: {build / "UrsaEngine.slnx"}')
    print('Open in VS2026. Build UrsaEditor or UrsaScriptPlayer; IDE script builds only check compilation.')


if __name__ == '__main__':
    main_entry(main)
