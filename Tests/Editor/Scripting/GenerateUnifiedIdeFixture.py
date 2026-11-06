"""Create an independent debugger solution without rewriting the main workspace."""
import argparse
from pathlib import Path
import shutil
import sys
import xml.etree.ElementTree as ET
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'Tools'))
from AutomationSupport import expect, read_json
from WorkspaceCommon import main_entry


def generate(build_directory, project_directory):
    build, project = Path(build_directory).resolve(), Path(project_directory).resolve()
    workspace = read_json(build / 'UnifiedWorkspace.json')
    expect(workspace['Ready'] and workspace['Solution'].endswith('.slnx'), 'Generate a ready unified workspace before creating the debugger fixture.')
    source_root, fixture = project / 'Library/ScriptIDE', project / 'Library/UnifiedIDE'
    projects = {name: fixture / f'Projects/{group}/{name}.csproj' for group, name in (('Game', 'Ursa.Game'), ('Editor', 'Ursa.EditorScripts'))}
    for name, target in projects.items():
        group = 'Game' if name == 'Ursa.Game' else 'Editor'
        source = source_root / f'Projects/{group}/{name}.csproj'
        xml = ET.parse(source)
        for node in xml.findall('.//Compile[@Include]'):
            path = (source.parent / node.attrib['Include']).resolve()
            expect(path.is_file(), f'Fixture source is missing: {path}')
            node.set('Include', str(path))
        for items in xml.getroot():
            for node in list(items):
                if node.tag == 'Reference':
                    assembly = node.attrib['Include']
                    path = projects.get(assembly, Path(workspace['ManagedProjects']) / assembly / f'{assembly}.csproj')
                    expect(assembly in projects or path.is_file(), f'Unknown fixture framework: {assembly}')
                    index = list(items).index(node)
                    items.remove(node)
                    items.insert(index, ET.Element('ProjectReference', Include=str(path)))
        for node in xml.findall('.//ProjectReference[@Include]'):
            assembly = Path(node.attrib['Include']).stem
            node.set('Include', str(projects.get(assembly, Path(workspace['ManagedProjects']) / assembly / f'{assembly}.csproj')))
        target.parent.mkdir(parents=True, exist_ok=True)
        xml.write(target, encoding='utf-8', xml_declaration=False)
    shutil.copy2(source_root / 'Directory.Build.props', fixture / 'Directory.Build.props')
    xml = ET.parse(workspace['Solution'])
    for node in xml.iter():
        attribute = 'Path' if node.tag == 'Project' else 'Project' if node.tag == 'BuildDependency' else None
        if attribute and attribute in node.attrib:
            path = Path(node.attrib[attribute])
            node.set(attribute, projects.get(path.stem, (build / path).resolve()).as_posix())
        if node.tag == 'Folder' and node.get('Name', '').startswith('/Projects/') and node.get('Name') != '/Projects/':
            node.set('Name', '/Projects/Acceptance/')
    solution = fixture / 'UrsaEngine.slnx'
    xml.write(solution, encoding='utf-8', xml_declaration=False)
    return solution


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', required=True)
    parser.add_argument('--project-directory', required=True)
    args = parser.parse_args()
    print(generate(args.build_directory, args.project_directory))


if __name__ == '__main__':
    main_entry(main)
