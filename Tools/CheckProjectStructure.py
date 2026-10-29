"""Check source membership, filters and references in a generated VS workspace."""

import argparse
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

from ReadVisualStudioSolution import read_projects
from WorkspaceCommon import full_path, main_entry


def inside(path, root):
    return full_path(path).is_relative_to(full_path(root))


def check(build_dir):
    source_root = full_path(Path(__file__).parent / '..')
    build_root = full_path(build_dir)
    cache = (build_root / 'CMakeCache.txt').read_text(encoding='utf-8-sig')
    if 'CMAKE_GENERATOR:INTERNAL=Visual Studio' not in cache:
        raise RuntimeError('This check requires a Visual Studio generator')
    manifest = json.loads((build_root / 'UrsaProjectStructure.json').read_text(encoding='utf-8-sig'))
    projects = read_projects(build_root)
    registered, checked = set(), 0
    for target in manifest['targets']:
        name = target['name']
        if name not in projects:
            raise RuntimeError(f'Missing VS project: {name}')
        project = projects[name]
        if project['folder'] != target['folder']:
            raise RuntimeError(f'Wrong solution folder for {name}: {project["folder"]}')
        xml = ET.parse(project['path']).getroot()
        filters = ET.parse(str(project['path']) + '.filters').getroot()
        filter_map = {node.attrib['Include']: next((child.text or '' for child in node if child.tag.split('}')[-1] == 'Filter'), '')
                      for node in filters.iter() if 'Include' in node.attrib}
        for node in xml.iter():
            kind = node.tag.split('}')[-1]
            if kind not in ('ClCompile', 'ClInclude') or 'Include' not in node.attrib:
                continue
            include = node.attrib['Include']
            path = full_path(project['path'].parent / include)
            if not inside(path, source_root) or inside(path, source_root / 'ThirdParty'):
                continue
            if not path.is_file():
                raise RuntimeError(f'Missing source on disk: {path}')
            relative = path.relative_to(source_root)
            expected = str(relative.parent) if relative.parent != Path('.') else ''
            actual = filter_map.get(include, '')
            if actual != expected:
                raise RuntimeError(f'Wrong filter for {relative} : {actual} (expected {expected})')
            if kind == 'ClCompile' and path in registered:
                raise RuntimeError(f'Source compiled in multiple targets: {relative}')
            registered.add(path)
            checked += 1
    editor = 'URSA_BUILD_EDITOR:BOOL=ON' in cache
    dotnet = 'URSA_ENABLE_DOTNET:BOOL=ON' in cache
    tests = 'URSA_BUILD_TESTS:BOOL=ON' in cache
    for directory in ('Runtime', 'Engine', 'Editor', 'Tools', 'Tests'):
        if (directory == 'Editor' and not editor) or (directory == 'Tests' and not tests):
            continue
        for file in (source_root / directory).rglob('*'):
            if file.suffix.lower() not in ('.cpp', '.hpp', '.h', '.inl') or not file.is_file():
                continue
            relative = file.relative_to(source_root).as_posix()
            if relative.startswith(('Tools/Python/', 'Tools/TracyMcp/Release/')):
                continue
            if not dotnet and relative.startswith('Engine/Scripting/DotNet/'):
                continue
            if not editor and re.match(r'^Tools/(AssetImport|AssetMigrate|MeshMigrate|ScriptCompile|ScriptPlayer)/', relative):
                continue
            if not dotnet and re.match(r'^Tools/(ScriptCompile|ScriptPlayer)/', relative):
                continue
            if (not editor or not dotnet) and relative.startswith('Tools/IdeBridge/'):
                continue
            if relative.startswith('Tests/'):
                if not dotnet and relative.startswith(('Tests/Bindings/', 'Tests/Editor/Scripting/')):
                    continue
                if not editor and (relative.startswith('Tests/Editor/') or relative == 'Tests/Headers/MaterialInspectorHeaderSmoke.cpp'):
                    continue
                if relative.startswith('Tests/Integration/'):
                    if re.match(r'^Tests/Integration/(DotNetHostTests.cpp$|DotNetHost/)', relative):
                        if not dotnet:
                            continue
                    elif not dotnet or not editor or re.search(r'^URSA_PROJECT_DIR:PATH=$', cache, re.M):
                        continue
            if file not in registered:
                raise RuntimeError(f'Unregistered source: {relative}')
    if not tests and any(re.search(r'(Tests|Smoke|Benchmarks)$', name) or name == 'UrsaManagedTestAssemblies' for name in projects):
        raise RuntimeError('Test targets leaked into a production-only build')
    for name in ('EASTL', 'TracyClient', 'glfw', 'spdlog', 'assimp', 'zlibstatic', 'UrsaImGui'):
        if name in projects and not projects[name]['folder'].startswith('ThirdParty'):
            raise RuntimeError(f'Dependency is misclassified: {name}')
    managed_count = 0
    managed_manifest = build_root / 'Ide/Managed/Projects.json'
    if dotnet and managed_manifest.is_file():
        for entry in json.loads(managed_manifest.read_text(encoding='utf-8-sig')):
            name = entry['name']
            if name not in projects:
                raise RuntimeError(f'Missing managed VS project: {name}')
            project = projects[name]
            if project['folder'] != entry['folder'] or project['path'] != full_path(entry['path']):
                raise RuntimeError(f'Wrong managed project mapping: {name}')
            xml = ET.parse(project['path']).getroot()
            for item in xml.findall('.//Compile[@Include]'):
                include = item.attrib['Include']
                if '$(' in include or '*' in include:
                    continue
                if not Path(include).is_absolute() or not Path(include).is_file():
                    raise RuntimeError(f'Managed source is not an existing original file: {include}')
                if inside(include, build_root):
                    raise RuntimeError(f'Managed IDE source points into build output: {include}')
            for item in xml.findall('.//ProjectReference[@Include]'):
                name = Path(item.attrib['Include']).stem
                if name not in projects:
                    raise RuntimeError(f'Unregistered managed reference: {name}')
            if project['node'] is not None and project['node'].find("Platform[@Project='Any CPU']") is None:
                raise RuntimeError(f'Missing managed platform mapping: {entry["name"]}')
            managed_count += 1
        description_path = build_root / 'UnifiedWorkspace.json'
        if description_path.is_file():
            description = json.loads(description_path.read_text(encoding='utf-8-sig'))
            if description['Ready']:
                for name in ('Ursa.Game', 'Ursa.EditorScripts'):
                    if name not in projects:
                        raise RuntimeError(f'Missing project script VS project: {name}')
                    path = projects[name]['path']
                    xml = ET.parse(path).getroot()
                    if xml.findall('.//Reference') or not xml.findall('.//ProjectReference'):
                        raise RuntimeError(f'Unified script project still uses binary framework references: {name}')
                    for item in xml.findall('.//Compile[@Include]'):
                        source = full_path(path.parent / item.attrib['Include'])
                        if not inside(source, Path(description['ProjectRoot']) / 'Assets') or not source.is_file():
                            raise RuntimeError(f'Script project does not link an original asset: {source}')
    print(f'Checked {len(manifest["targets"])} first-party targets, {checked} C++ file entries and '
          f'{managed_count} SDK C# projects; source paths, folders and references match.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True)
    check(parser.parse_args().build_dir)


if __name__ == '__main__':
    main_entry(main)
