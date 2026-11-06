"""Verify independent debugger projects, references and original source membership."""
import argparse
from pathlib import Path
import sys
import uuid
import xml.etree.ElementTree as ET
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Tools'))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Editor/Scripting'))
from AutomationSupport import expect, read_json, run, sha256
from GenerateUnifiedIdeFixture import generate
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    args = parser.parse_args()
    build = args.build_directory.resolve()
    descriptor = build / 'UnifiedWorkspace.json'
    workspace = read_json(descriptor)
    before = sha256(workspace['Solution']), sha256(descriptor)
    root = build / 'Temp' / ('UnifiedIdeFixture-' + uuid.uuid4().hex)
    (root / 'Assets/Editor').mkdir(parents=True)
    (root / 'Assets/Probe.cs').write_text('using Ursa; public sealed class Probe : Behaviour {}', encoding='utf-8')
    (root / 'Assets/Editor/Tool.cs').write_text('public static class Tool {}', encoding='utf-8')
    run([args.compiler, root, '--ide'])
    source_project = root / 'Library/ScriptIDE/Projects/Game/Ursa.Game.csproj'
    source_hash = sha256(source_project)
    solution = ET.parse(generate(build, root))
    expect((sha256(workspace['Solution']), sha256(descriptor)) == before, 'Debugger fixture changed main workspace')
    expect(sha256(source_project) == source_hash, 'Debugger fixture changed host IDE workspace')
    membership = {Path(node.attrib['Path']).resolve() for node in solution.findall('.//Project[@Path]')}
    expect(all(p.is_absolute() and p.is_file() for p in membership), 'Fixture project is not an existing absolute path')
    for group in ('Game/Ursa.Game', 'Editor/Ursa.EditorScripts'):
        path = root / f'Library/UnifiedIDE/Projects/{group}.csproj'
        project = ET.parse(path)
        expect(not project.findall('.//Reference/HintPath'), 'Fixture leaked binary framework references')
        for node in project.findall('.//Compile[@Include]'):
            source = Path(node.attrib['Include']).resolve()
            expect(source.is_relative_to(root / 'Assets') and source.is_file(), 'Fixture source outside real Assets')
        expect(path.resolve() in membership, 'Fixture C# project missing from solution')
    print('Independent solution, original source membership, framework references and main-workspace isolation passed.')


if __name__ == '__main__':
    main_entry(main)
