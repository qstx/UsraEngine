"""Prepare the interactive suite without sharing Sample assets or caches."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import uuid


def prepare(build_root: Path, project: Path, source: Path) -> None:
    build_root, project, source = build_root.resolve(), project.resolve(), source.resolve()
    allowed = build_root / 'TestProjects' / 'DemoSuite'
    if project.parent != allowed or not project.name.replace('_', '').replace('-', '').isalnum():
        raise ValueError(f'Refusing to clear a non-test DemoSuite project: {project}')
    if not (source / 'Assets/Tests/Menu.ursascene').is_file():
        raise ValueError(f'Demo suite source is unavailable: {source}')
    for folder in ('Assets', 'Library', 'ProjectSettings'):
        target = project / folder
        if target.resolve().parent != project:
            raise ValueError(f'Refusing to clear an escaped DemoSuite directory: {target.resolve()}')
    for folder in ('Assets', 'Library', 'ProjectSettings'):
        target = project / folder
        if target.exists():
            shutil.rmtree(target)
    shutil.copytree(source / 'Assets', project / 'Assets')
    (project / 'ProjectSettings').mkdir()
    for filename in ('BuildSettings.json', 'ManagedImportRules.json'):
        path = source / 'ProjectSettings' / filename
        if path.is_file():
            shutil.copy2(path, project / 'ProjectSettings' / filename)
    validate_catalog(project)


def validate_catalog(project: Path) -> None:
    assets = project / 'Assets'
    identities = {}
    for path in assets.rglob('*.meta'):
        value = json.loads(path.read_text(encoding='utf-8-sig'))
        guid = str(uuid.UUID(value['guid']))
        if guid in identities:
            raise ValueError(f'Duplicate asset identity: {path} and {identities[guid]}')
        identities[guid] = path
    settings = json.loads((project / 'ProjectSettings/BuildSettings.json').read_text(encoding='utf-8-sig'))
    enabled = [entry['scene'] for entry in settings['scenes'] if entry.get('enabled', True)]
    catalog = json.loads((assets / 'Tests/TestCatalog.ursaasset').read_text(encoding='utf-8-sig'))['payload']['Fields']
    ids, titles, descriptions, scenes = (catalog[key] for key in ('ids', 'titles', 'descriptions', 'scenes'))
    if len({len(ids), len(titles), len(descriptions), len(scenes)}) != 1 or len(set(ids)) != len(ids) or any(not item.strip() for item in ids + titles):
        raise ValueError('Invalid or duplicate test catalog metadata')
    identity = lambda item: (item['guid'], item['localFileId'])
    expected = [catalog['menuScene'], *scenes]
    if len(set(map(identity, expected))) != len(expected) or list(map(identity, expected)) != list(map(identity, enabled)):
        raise ValueError('TestCatalog and enabled BuildSettings Scene order differ')
    scene_assets = {}
    for path in assets.rglob('*.ursascene'):
        document = json.loads(path.read_text(encoding='utf-8-sig'))
        meta = json.loads(path.with_name(path.name + '.meta').read_text(encoding='utf-8-sig'))
        if document.get('format') != 'UrsaScene' or document['id'] != meta['guid']:
            raise ValueError(f'Invalid Scene identity: {path}')
        scene_assets[(meta['guid'], 0)] = path
    if set(scene_assets) != set(map(identity, expected)):
        raise ValueError('Missing, non-Scene or unregistered suite Scene')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-root', type=Path, required=True)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--importer', type=Path)
    parser.add_argument('--configuration', default='Debug')
    options = parser.parse_args()
    prepare(options.build_root, options.project, options.source)
    if options.importer:
        subprocess.run([str(options.importer), '--project', str(options.project), '--configuration', options.configuration], check=True)
    print('Prepared and validated isolated DemoSuite project:', options.project)
