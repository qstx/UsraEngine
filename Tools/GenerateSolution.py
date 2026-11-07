"""Generate the VS2026 C++/C# workspace, including game and Editor scripts.

Requires CMake 4.4.5+, VS2026/v145, .NET 10 and initialized third-party
submodules. Tests are omitted by default. The first run builds workspace
tools; later runs refresh incrementally. No PowerShell is needed.
Generates Debug, Release and Publish configurations. Tracy is enabled for
Debug/Release; Publish excludes it. Deep Profiling is controlled separately.
"""

import argparse
import os
from pathlib import Path
import sys

from WindowsToolchain import get_toolchain
from WorkspaceCommon import full_path, main_entry, run


def main():
    # 引擎和项目路径优先使用命令行参数，其次使用环境变量，最后采用仓库默认位置。
    default_engine = Path(__file__).parent.parent
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--engine-dir', default=os.environ.get('URSA_ENGINE_DIR'), help='engine checkout passed to CMake -S (default: URSA_ENGINE_DIR or this Engine)')
    parser.add_argument('--build-dir', help='build directory (default: selected engine/Build/VS2026)')
    parser.add_argument('--project-dir', default=os.environ.get('URSA_PROJECT_DIR'), help='game project directory (default: URSA_PROJECT_DIR or sibling Demo)')
    parser.add_argument('--cmake', default='cmake', help='CMake executable')
    parser.add_argument('--tests', action='store_true', help='include test projects')
    args = parser.parse_args()
    # 后续 CMake 和脚本工具共用规范化的绝对路径，支持从其他工作目录调用。
    engine_root = full_path(args.engine_dir or default_engine)
    build = full_path(args.build_dir or engine_root / 'Build/VS2026')
    project = full_path(args.project_dir or engine_root.parent / 'Demo')
    # 提前检查源码、项目资源和 Tracy 子模块，避免在构建阶段才发现输入缺失。
    if not (engine_root / 'CMakeLists.txt').is_file():
        raise RuntimeError(f'Engine CMakeLists.txt is missing: {engine_root}')
    if not (project / 'Assets').is_dir():
        raise RuntimeError(f'Project Assets directory is missing: {project}')
    if not (engine_root / 'ThirdParty/tracy/CMakeLists.txt').is_file():
        raise RuntimeError(f'Tracy submodule is missing. Run: git -C "{engine_root}" submodule update --init ThirdParty/tracy')
    # 验证 CMake、VS2026/v145，并拒绝复用不兼容的旧生成器缓存。
    toolchain = get_toolchain(args.cmake, build)
    print(f'{toolchain["Version"]}; VS2026/v145 at {toolchain["VisualStudio"]}', flush=True)
    # 两次配置复用同一组开关；Tracy 的 Debug/Release 启用与 Publish 剔除由 CMake 按配置处理。
    configure = [toolchain['CMake'], '-S', engine_root, '-B', build, '-G', toolchain['Generator'], '-A', 'x64', '-T', 'v145',
                 f'-DCMAKE_GENERATOR_INSTANCE={toolchain["VisualStudio"]}', '-DURSA_BUILD_EDITOR=ON',
                 '-DURSA_ENABLE_TRACY=ON', '-DURSA_MSVC_PARALLEL_COMPILE=ON', f'-DURSA_BUILD_TESTS={"ON" if args.tests else "OFF"}',
                 f'-DURSA_ENGINE_DIR={engine_root.as_posix()}', f'-DURSA_PROJECT_DIR={project.as_posix()}',
                 f'-DURSA_WORKSPACE_PYTHON={Path(sys.executable).as_posix()}']
    # 第一阶段：先引导工具工程，暂不要求尚未生成的项目 C# 工程已经存在。
    print('[1/5] Configure workspace tools', flush=True)
    run(configure + ['-DURSA_WORKSPACE_BOOTSTRAP=ON'])
    # 第二阶段：只构建 Debug 脚本工程生成工具，不编译游戏脚本或导入项目资源。
    print('[2/5] Build workspace tools (no game compilation or content import)', flush=True)
    run([toolchain['CMake'], '--build', build, '--config', 'Debug', '--parallel', '3', '--target', 'UrsaScriptCompile', '--', '/nodeReuse:false'])
    compiler, description = build / 'Debug/UrsaScriptCompile.exe', build / 'UnifiedWorkspace.json'
    ide = [compiler, project, '--ide', '--workspace', description]
    # 第三阶段：收集项目脚本并准备 C# 工程；完整工作区关联在第五阶段正式发布。
    print('[3/5] Collect project scripts and generate C# projects', flush=True)
    run(ide + ['--prepare-workspace'])
    # 第四阶段：为三个配置准备引擎 C# IDE 绑定，通过项目引用补齐依赖。
    # IDE 使用独立输出目录，不覆盖正式引擎框架或游戏发布产物。
    print('[4/5] Prepare IDE framework bindings and all three configurations', flush=True)
    for configuration in ('Debug', 'Release', 'Publish'):
        for name in ('Ursa.Editor', 'Ursa.ManagedHost'):
            run(['dotnet', 'build', build / f'Ide/Managed/{name}/{name}.csproj', '--configuration', configuration,
                 '--nologo', '-m:1', '--disable-build-servers', '-nr:false'])
    # 第五阶段：关闭引导模式，将原生和 C# 工程纳入统一解决方案，再更新项目关联。
    print('[5/5] Complete the unified solution and publish the association', flush=True)
    run(configure + ['-DURSA_WORKSPACE_BOOTSTRAP=OFF'])
    run(ide)
    # 最后校验源码归属、VS 分类和工程引用，确保生成结果完整且一致。
    run([sys.executable, engine_root / 'Tools/CheckProjectStructure.py', '--build-dir', build])
    print(f'Solution ready: {build / "UrsaEngine.slnx"}')
    print('Open in VS2026. Build UrsaEditor or UrsaScriptPlayer; IDE script builds only check compilation.')


if __name__ == '__main__':
    # 统一将工具执行失败或异常转换为退出码，供 BAT 和自动化判断。
    main_entry(main)
