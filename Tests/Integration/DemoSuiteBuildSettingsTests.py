"""Invalid source BuildSettings must not replace the last published Player snapshot."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
for name in ('importer', 'project', 'output'): parser.add_argument('--'+name, type=Path, required=True)
parser.add_argument('--configuration', default='Debug')
args = parser.parse_args()
project, output = args.project.resolve(), args.output.resolve()
if project.parent.name != 'DemoSuite' or project.parent.parent.name != 'TestProjects':
    raise ValueError('BuildSettings tests require the isolated DemoSuite test project')
output.mkdir(parents=True, exist_ok=True)
settings = project / 'ProjectSettings/BuildSettings.json'
original = settings.read_bytes()
catalog_path = project/'Assets/Tests/TestCatalog.ursaasset'
catalog_original = catalog_path.read_bytes()
baseline = json.loads(original)
ready_path = project / f'Library/RuntimeContent/WindowsVulkan/{args.configuration}/Player/Ready.json'
published = ready_path.read_bytes()
entries = {entry['path'] for entry in json.loads(published)['entries']}
required = {'prj:Tests/Menu.ursascene', 'prj:Tests/OnGUI/OnGUI.ursascene',
            'prj:__UrsaSettings/Scenes.json', 'ursa:Fonts/Runtime/NotoSansSC-VF.ttf',
            'ursa:Shaders/Runtime/GUI.ursashaderasset', 'prj:Tests/OnGUI/Checker.ursatex'}
assert required <= entries, required-entries
assert not any('Editor/' in entry or entry.endswith('Ursa.Editor.dll') for entry in entries)
cases = {}
changed = copy.deepcopy(baseline)
changed['scenes'][1]['scene']['guid'] = '00000000-0000-0000-0000-000000000001'
cases['missing'] = changed
changed = copy.deepcopy(baseline)
changed['scenes'][1] = copy.deepcopy(changed['scenes'][0])
cases['duplicate'] = changed
changed = copy.deepcopy(baseline)
catalog = json.loads((project/'Assets/Tests/TestCatalog.ursaasset.meta').read_text(encoding='utf-8-sig'))
changed['scenes'][1]['scene'] = {'guid':catalog['guid'], 'localFileId':0}
cases['non-scene'] = changed
try:
    for name, changed in cases.items():
        settings.write_text(json.dumps(changed), encoding='utf-8')
        result = subprocess.run([str(args.importer.resolve()), '--project', str(project), '--profile', 'Player',
            '--configuration', args.configuration], env=dict(os.environ, URSA_USER_DATA_ROOT=str(output/'user')),
            capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
        (output/(name+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
        assert result.returncode != 0, name
        assert ready_path.read_bytes() == published, f'{name} replaced the previous published content'
    settings.write_bytes(original)
    for case in ('empty-id', 'scene-mismatch'):
        catalog_document = json.loads(catalog_original)
        if case == 'empty-id': catalog_document['payload']['Fields']['ids'][0] = ''
        else: catalog_document['payload']['Fields']['scenes'][0] = baseline['scenes'][0]['scene']
        catalog_path.write_text(json.dumps(catalog_document), encoding='utf-8')
        result = subprocess.run([str(args.importer.resolve()), '--project', str(project), '--profile', 'Player',
            '--configuration', args.configuration], env=dict(os.environ, URSA_USER_DATA_ROOT=str(output/'user')),
            capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
        (output/(case+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
        assert result.returncode != 0 and ready_path.read_bytes() == published, case
finally:
    settings.write_bytes(original)
    catalog_path.write_bytes(catalog_original)
print('5 invalid BuildSettings/catalog cases preserved the exact published snapshot; enabled Scenes, runtime font/shader and texture are present, Editor payloads are absent.')
