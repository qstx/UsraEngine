"""Verify Python toolchain checks and managed IDE project generation."""
import argparse
from pathlib import Path
import sys
import uuid
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Tools'))
from AutomationSupport import expect, read_json
from GenerateManagedIde import generate
from WindowsToolchain import get_toolchain
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine-root', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    args = parser.parse_args()
    root = args.scratch.resolve() / ('Run-' + uuid.uuid4().hex)
    root.mkdir(parents=True)
    cache = root / 'OldCache/CMakeCache.txt'
    cache.parent.mkdir()
    cache.write_text('CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022', encoding='utf-8')
    try:
        get_toolchain(build_directory=cache.parent)
        raise AssertionError('Old generator cache was accepted')
    except RuntimeError as error:
        expect('Incompatible build cache' in str(error), str(error))
    expect('2022' in cache.read_text(encoding='utf-8-sig'), 'Old cache was modified')
    old_cmake = root / 'OldCMake.cmd'
    old_cmake.write_text('@echo off\necho cmake version 3.28.0\nexit /b 0\n', encoding='utf-8')
    try:
        get_toolchain(cmake=old_cmake)
        raise AssertionError('Old CMake was accepted')
    except RuntimeError as error:
        expect('4.4.5 or newer' in str(error), str(error))
    fixture, build, project = root / 'Fixture', root / 'Generated', root / 'Project'
    module = fixture / 'Managed/Probe'
    module.mkdir(parents=True)
    (module / 'Probe.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework><AllowUnsafeBlocks>true</AllowUnsafeBlocks><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Reference Include="Microsoft.CodeAnalysis"><HintPath>$(MSBuildSDKsPath)/../Roslyn/bincore/Microsoft.CodeAnalysis.dll</HintPath></Reference></ItemGroup></Project>', encoding='utf-8')
    (module / 'A.cs').write_text('class A {}', encoding='utf-8')
    def invoke(**options):
        generate(engine_root=fixture, build_root=build, **options)
    invoke()
    path = build / 'Ide/Managed/Probe/Probe.csproj'
    before, timestamp = path.read_text(encoding='utf-8-sig'), path.stat().st_mtime_ns
    invoke()
    expect(path.stat().st_mtime_ns == timestamp, 'Unchanged generation rewrote project')
    expect('<AllowUnsafeBlocks>true</AllowUnsafeBlocks>' in before and '$(MSBuildSDKsPath)' in before, 'Project metadata or SDK reference lost')
    expect((module / 'A.cs').as_posix() in before and 'Link="A.cs"' in before, 'Original source link lost')
    added = module / 'Added.cs'
    added.write_text('class Added {}', encoding='utf-8'); invoke()
    expect('Added.cs' in path.read_text(encoding='utf-8-sig'), 'Added source omitted')
    added.unlink(); invoke()
    expect(path.read_text(encoding='utf-8-sig') == before, 'Removed source retained')
    tests = fixture / 'Tests/Managed/Ursa.SnakeLogicTests'
    tests.mkdir(parents=True)
    (tests / 'Ursa.SnakeLogicTests.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework></PropertyGroup><ItemGroup><Compile Include="$(UrsaProjectDir)/Assets/Scripts/Snake/SnakeGame.cs" /></ItemGroup></Project>', encoding='utf-8')
    project.mkdir()
    invoke(include_tests=True, project_directory=str(project))
    manifest = build / 'Ide/Managed/Projects.json'
    expect('Ursa.SnakeLogicTests' not in manifest.read_text(encoding='utf-8-sig'), 'Different project included Demo source tests')
    snake = project / 'Assets/Scripts/Snake/SnakeGame.cs'
    snake.parent.mkdir(parents=True); snake.write_text('class SnakeGame {}', encoding='utf-8')
    invoke(include_tests=True, project_directory=str(project))
    expect('Ursa.SnakeLogicTests' in manifest.read_text(encoding='utf-8-sig'), 'Available Demo tests omitted')
    expect(f'<UrsaProjectDir>{project}</UrsaProjectDir>' in (build / 'Ide/Managed/Ursa.SnakeLogicTests/Ursa.SnakeLogicTests.csproj').read_text(encoding='utf-8-sig'), 'Project source root lost')
    props = build / 'Ide/Managed/Directory.Build.props'
    stamp = props.stat().st_mtime_ns
    invoke(include_tests=True)
    expect(props.stat().st_mtime_ns == stamp, 'Switching game rewrote shared properties')
    print('Toolchain rejection, source links, metadata, additions/removals and no-op generation verified.')


if __name__ == '__main__':
    main_entry(main)
