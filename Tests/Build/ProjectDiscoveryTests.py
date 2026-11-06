"""Verify normal CMake builds discover source additions and removals."""
import argparse
from pathlib import Path
import shutil
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Tools'))
from AutomationSupport import expect, inside, run
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--engine-root', type=Path, required=True)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--generator', default='Visual Studio 18 2026')
    args = parser.parse_args()
    source, build = args.scratch.resolve() / 'Source', args.scratch.resolve() / 'Build'
    module = source / 'Runtime/Core'
    module.mkdir(parents=True, exist_ok=True)
    (source / 'CMake').mkdir(exist_ok=True)
    shutil.copy2(args.engine_root / 'CMake/SourceGroups.cmake', source / 'CMake/SourceGroups.cmake')
    (source / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 4.4.5)\nproject(DiscoveryProbe LANGUAGES CXX)\nset(URSA_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")\ninclude(CMake/SourceGroups.cmake)\nursa_collect_sources(sources "${CMAKE_CURRENT_SOURCE_DIR}/Runtime")\nadd_library(DiscoveryProbe STATIC ${sources})\nursa_source_groups(DiscoveryProbe "Runtime")\n', encoding='utf-8')
    (module / 'Base.cpp').write_text('int Base() { return 1; }', encoding='utf-8')
    print(run([args.cmake, '-S', source, '-B', build, '-G', args.generator, '-A', 'x64', '-T', 'v145']))
    def compile_probe():
        print(run([args.cmake, '--build', build, '--config', 'Debug', '--parallel', '--', '/nodeReuse:false']))
    compile_probe()
    paths = [module / f'Added.{extension}' for extension in ('cpp', 'hpp', 'h', 'inl')]
    for path in paths:
        path.write_text('int Added() { return 2; }' if path.suffix == '.cpp' else '// Source discovery fixture', encoding='utf-8')
    compile_probe()
    project, filters = (build / 'DiscoveryProbe.vcxproj').read_text(encoding='utf-8-sig'), (build / 'DiscoveryProbe.vcxproj.filters').read_text(encoding='utf-8-sig')
    for path in paths:
        expect(str(path) in project and str(path) in filters, f'Added file not discovered: {path}')
    expect('<Filter>Runtime\\Core</Filter>' in filters, 'Directory filter did not match disk')
    for path in paths:
        inside(path, source).unlink()
    compile_probe()
    project = (build / 'DiscoveryProbe.vcxproj').read_text(encoding='utf-8-sig')
    expect(all(str(path) not in project for path in paths), 'Deleted source retained in generated project')
    print('Normal builds discovered additions and removals of all supported code extensions.')


if __name__ == '__main__':
    main_entry(main)
