"""Validate the public RiggedSimple GLB and its imported mesh/skin/animation artifacts."""
import argparse
from pathlib import Path
import shutil
import urllib.request
from AutomationSupport import ENGINE, environment, expect, read_json, run, sha256
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model-path', type=Path)
    parser.add_argument('--build-dir', type=Path, default=ENGINE / 'Build')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    validator, importer = build / 'Debug/UrsaModelImportCandidateTests.exe', build / 'Editor/Debug/UrsaAssetImport.exe'
    for executable in (validator, importer):
        expect(executable.is_file(), f'Build {executable.name} before running this validator.')
    model = args.model_path or build / 'ExternalSamples/RiggedSimple.glb'
    if args.model_path is None and not model.exists():
        model.parent.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve('https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/RiggedSimple/glTF-Binary/RiggedSimple.glb', model)
    model = model.resolve()
    expect(model.suffix.lower() == '.glb' and model.stat().st_size > 0, 'This smoke validator expects a nonempty self-contained .glb file.')
    print(f'Validating {model} (SHA-256 {sha256(model)})')
    print(run([validator, '--external-glb', model]))
    project = build / 'ExternalSamples/RiggedSimpleProject'
    assets, settings = project / 'Assets', project / 'ProjectSettings'
    assets.mkdir(parents=True, exist_ok=True); settings.mkdir(parents=True, exist_ok=True)
    shutil.copy2(model, assets / 'RiggedSimple.glb')
    (settings / 'ManagedImportRules.json').write_text('{"format":"UrsaManagedImportRules","version":1,"required":[],"sources":[]}', encoding='utf-8')
    temporary = build / 'Temp/ShaderCompiler'
    temporary.mkdir(parents=True, exist_ok=True)
    with environment(TEMP=temporary, TMP=temporary):
        print(run([importer, '--project', project, '--engine', ENGINE / 'Assets', '--configuration', 'Debug', '--profile', 'Editor', '--no-scripts']))
    database = read_json(project / 'Library/AssetDatabase/prj.json')
    imports = [item for item in database['imports'] if item['importer'] == 'ursa.Model' and
               any(a['outputKey'] == 'main' for a in item['artifacts'])]
    expect(imports, 'Model import is missing.')
    imported = imports[-1]
    expect(imported['target'] == 'model-v9' and {'Mesh', 'Skeleton', 'AnimationClip'} <= {a['kind'] for a in imported['artifacts']},
           'UrsaAssetImport did not publish the public model, mesh, skeleton and animation artifacts.')
    print(f'UrsaAssetImport published model-v9 with {len(imported["artifacts"])} sub-assets to {project / "Library"}')


if __name__ == '__main__':
    main_entry(main)
