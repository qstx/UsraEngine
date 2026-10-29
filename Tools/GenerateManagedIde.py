"""Generate SDK IDE projects that link the original managed sources."""

import argparse
import hashlib
import json
from pathlib import Path
import uuid
import xml.etree.ElementTree as ET

from WorkspaceCommon import full_path, main_entry, write_changed


def source_files(directory, pattern):
    return sorted(path for path in directory.rglob(pattern)
                  if not {'bin', 'obj'}.intersection(path.relative_to(directory).parts))


def generate(engine_root, build_root, include_tests=False, roslyn_directory='', project_directory=''):
    engine_root, build_root = full_path(engine_root), full_path(build_root)
    ide_root = build_root / 'Ide/Managed'
    inputs = source_files(engine_root / 'Managed', '*.csproj')
    if include_tests:
        inputs += source_files(engine_root / 'Tests/Managed', '*.csproj')
    if not project_directory or not (Path(project_directory) / 'Assets/Scripts/Snake/SnakeGame.cs').is_file():
        inputs = [path for path in inputs if path.stem != 'Ursa.SnakeLogicTests']
    projects = {}
    for path in sorted(inputs):
        name = path.stem
        if name in projects:
            raise RuntimeError(f'Duplicate managed project name: {name}')
        digest = hashlib.sha256(f'Ursa/Managed/{name}'.encode()).digest()
        projects[name] = dict(name=name, path=(ide_root / name / f'{name}.csproj').as_posix(),
                              guid=str(uuid.UUID(bytes_le=digest[:16])).upper(),
                              folder='Tests/Managed' if 'Tests' in path.relative_to(engine_root).parts else 'Managed/Framework',
                              sourceProject=path.as_posix())
    props = ET.Element('Project')
    group = ET.SubElement(props, 'PropertyGroup')
    for name, value in {
        'BaseIntermediateOutputPath': f'{ide_root.as_posix()}/Build/$(MSBuildProjectName)/$(Configuration)/obj/',
        'OutputPath': f'{ide_root.as_posix()}/Build/$(MSBuildProjectName)/$(Configuration)/bin/',
        'AppendTargetFrameworkToOutputPath': 'false', 'EnableDefaultCompileItems': 'false',
        'UrsaBindingsSourceDir': (engine_root / 'Managed').as_posix(),
        'UrsaBindingsGeneratedDir': f'{ide_root.as_posix()}/Build/$(MSBuildProjectName)/$(Configuration)/Bindings',
        'UrsaBindingsGenerator': f'{ide_root.as_posix()}/Build/Ursa.BindingsGenerator/$(Configuration)/bin/Ursa.BindingsGenerator.dll',
    }.items():
        ET.SubElement(group, name).text = value
    write_changed(ide_root / 'Directory.Build.props', ET.tostring(props, encoding='unicode') + '\n')
    targets = ET.Element('Project')
    ET.SubElement(targets, 'Import', Project=(engine_root / 'Managed/Directory.Build.targets').as_posix())
    write_changed(ide_root / 'Directory.Build.targets', ET.tostring(targets, encoding='unicode') + '\n')
    for entry in projects.values():
        source = Path(entry['sourceProject'])
        # Preserve comments and all original build metadata.
        xml = ET.fromstring(source.read_text(encoding='utf-8-sig'),
                            parser=ET.XMLParser(target=ET.TreeBuilder(insert_comments=True)))
        if '$(UrsaProjectDir)' in ET.tostring(xml, encoding='unicode'):
            ET.SubElement(ET.SubElement(xml, 'PropertyGroup'), 'UrsaProjectDir').text = project_directory
        for hint in xml.findall('.//Reference/HintPath'):
            if roslyn_directory and hint.text:
                hint.text = hint.text.replace('$(MSBuildSDKsPath)/../Roslyn/bincore', roslyn_directory)
        for reference in xml.findall('.//ProjectReference[@Include]'):
            name = Path(reference.attrib['Include'].replace('\\', '/')).stem
            if name not in projects:
                raise RuntimeError(f'Unknown project reference: {name} in {entry["name"]}')
            reference.set('Include', projects[name]['path'])
        for compile_item in xml.findall('.//Compile[@Include]'):
            include = compile_item.attrib['Include']
            if '$(' not in include and not Path(include).is_absolute():
                compile_item.set('Include', full_path(source.parent / include).as_posix())
        items = ET.SubElement(xml, 'ItemGroup')
        for file in source_files(source.parent, '*.cs'):
            ET.SubElement(items, 'Compile', Include=file.as_posix(), Link=file.relative_to(source.parent).as_posix())
        if entry['name'] in ('Ursa.Runtime', 'Ursa.Rendering', 'Ursa.Editor', 'Ursa.ManagedHost'):
            ET.SubElement(items, 'ProjectReference', Include=projects['Ursa.BindingsGenerator']['path'], ReferenceOutputAssembly='false')
        write_changed(entry['path'], ET.tostring(xml, encoding='unicode') + '\n')
    write_changed(ide_root / 'Projects.json', json.dumps(sorted(projects.values(), key=lambda x: x['name']), indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine-root', required=True)
    parser.add_argument('--build-root', required=True)
    parser.add_argument('--include-tests', action='store_true')
    parser.add_argument('--roslyn-directory', default='')
    parser.add_argument('--project-directory', default='')
    args = parser.parse_args()
    generate(**vars(args))


if __name__ == '__main__':
    main_entry(main)
