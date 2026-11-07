"""Verify empty-project defaults, retired assets and independent SRP runtime content."""

import argparse
import copy
import json
from pathlib import Path
import shutil
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Tools'))
from AutomationSupport import environment, expect, inside, read_json, run, sha256, write_json
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('importer', 'player', 'package-tool', 'source', 'scratch'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--configuration', required=True)
    args = parser.parse_args()
    root = args.scratch.resolve() / uuid.uuid4().hex[:8]
    project = root / 'Project'
    assets = project / 'Assets'
    assets.mkdir(parents=True)
    original = read_json(args.source / 'ScriptSample/ScriptPlayer.ursascene')
    scene = copy.deepcopy(original)
    scene['gameObjects'] = [obj for obj in scene['gameObjects']
                            if all(item['type'] != 'ursa.ScriptBehaviour' for item in obj['components'])]
    for resource in scene['localResources']:
        resource['data']['textures'] = {}
        resource['data']['propertyOverrides'].pop('_BaseMap', None)
    scene_path = assets / 'Default.ursascene'
    write_json(scene_path, scene)
    imports = ['--project', project, '--configuration', args.configuration]
    # A broken project script cannot prevent the framework-only bootstrap catalog.
    invalid = assets / 'Invalid.cs'
    invalid.write_text('public class Broken { this is not valid C#; }', encoding='utf-8')
    run([args.importer, *imports, '--bootstrap', '--profile', 'Editor'])
    invalid.unlink()
    Path(str(invalid) + '.meta').unlink(missing_ok=True)
    run([args.importer, *imports])
    graphics_path = project / 'ProjectSettings/Graphics.json'
    expect(not graphics_path.exists(), 'Default selection implicitly saved project Graphics settings')
    ready_path = project / f'Library/RuntimeContent/WindowsVulkan/{args.configuration}/Player/Ready.json'

    def ready_content():
        ready = read_json(ready_path)
        entries = {entry['path']: entry for entry in ready['entries']}
        for name in ('ursa:Rendering/DefaultUniversalPipeline.ursaasset',
                     'ursa:Rendering/DefaultUniversalRenderer.ursaasset',
                     'ursa:Managed/FrameworkTypeCatalog.json'):
            expect(name in entries, f'Default runtime dependency was omitted: {name}')
        return ready, entries

    _, entries = ready_content()
    manifest_entry = entries['prj:__UrsaManaged/Game.json']
    manifest_path = inside(project / 'Library' / manifest_entry['storage'][4:], project / 'Library')
    expect(read_json(manifest_path)['Assemblies'] == [], 'Empty project unnecessarily requires a game assembly')

    def deploy(label):
        deployed = root / label
        deployed.mkdir()
        executable = deployed / args.player.name
        shutil.copy2(args.player, executable)
        shutil.copy2(args.player.parent / 'nethost.dll', deployed / 'nethost.dll')
        package = deployed / 'Content.pak'
        run([args.package_tool, '--project-root', project, '--configuration', args.configuration, '--output', package])
        ready, _ = ready_content()
        for entry in ready['entries']:
            scheme, relative = entry['path'].split(':', 1)
            expect(scheme in ('ursa', 'prj'), f'Unexpected runtime scheme {scheme}')
            destination = inside(deployed / 'Content' / ('Engine' if scheme == 'ursa' else 'Project') / relative,
                                 deployed)
            destination.parent.mkdir(parents=True, exist_ok=True)
            source = inside(project / 'Library' / entry['storage'][4:], project / 'Library')
            shutil.copy2(source, destination)
        expect(not any(p.suffix in ('.cs', '.hlsl', '.hlsli', '.csproj') for p in deployed.rglob('*')),
               'Standalone runtime content includes source/compiler inputs')
        expect(not any(p.name == 'Ursa.Editor.dll' for p in deployed.rglob('*')), 'Player deployed Editor API')
        return deployed, executable, package

    def play(deployed, executable, package=None, expected=0, capture=None):
        selection = ['--pak', package] if package else ['--content', deployed / 'Content']
        command = [executable, *selection, '--scene', 'Default.ursascene', '--hidden', '--frames', '4']
        if capture:
            command += ['--capture', capture, '--capture-frame', '3']
        with environment(URSA_USER_DATA_ROOT=deployed / 'User'):
            output = run(command, expected=expected, cwd=deployed)
        if capture and expected == 0:
            expect(capture.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'), 'Missing rendered runtime capture')
        return output

    default_dir, default_exe, default_pak = deploy('DefaultRuntime')
    baseline = default_dir / 'Default.png'
    play(default_dir, default_exe, capture=baseline)
    default_pak_capture = default_dir / 'DefaultPak.png'
    play(default_dir, default_exe, default_pak, capture=default_pak_capture)
    expect(sha256(baseline) == sha256(default_pak_capture), 'Default loose/Pak output differs')

    # Retired explicit assets must fail without being rewritten or publishing a fallback.
    retired = assets / 'Retired.ursaasset'
    write_json(retired, {'format': 'UrsaScriptableAsset', 'version': 1, 'localFileId': 1,
                        'typeId': 'ursa.rendering.forward', 'typeVersion': 1,
                        'payload': {'FormatVersion': 1, 'ScriptType': 'ursa.rendering.forward', 'Version': 1,
                                    'SourceGuid': 'c3db9aad-cd0d-4d07-a4dd-4f8a056a7410', 'LocalFileId': 1,
                                    'Fields': {}, 'FieldTypes': {}}})
    old_bytes, old_ready = retired.read_bytes(), ready_path.read_bytes()
    diagnostic = run([args.importer, *imports], expected=1)
    expect('has been removed' in diagnostic and 'UniversalRenderPipelineAsset' in diagnostic,
           f'Retired asset had no actionable migration diagnostic: {diagnostic}')
    expect(retired.read_bytes() == old_bytes and ready_path.read_bytes() == old_ready,
           'Retired asset failure rewrote author content or published fallback content')
    retired.unlink()
    Path(str(retired) + '.meta').unlink(missing_ok=True)

    # Exercise an explicitly selected project SRP and reset to the published default at runtime.
    source = assets / 'CustomAndReset.cs'
    source.write_text('''using System;
using System.Collections.Generic;
using Ursa;
using Ursa.Rendering;
namespace RetirementTest;
public sealed class CustomAsset : RenderPipelineAsset
{
    protected override RenderPipeline CreatePipeline() => new CustomPipeline();
    private sealed class CustomPipeline : RenderPipeline
    {
        public override void Render(ScriptableRenderContext context, List<Camera> cameras)
        {
            foreach (Camera camera in cameras)
            {
                context.SetupCameraProperties(camera);
                using var commands = new CommandBuffer("Custom retirement test");
                commands.ClearTarget(true, true, false, new Vector4(0, 0, 0, 1));
                context.ExecuteCommandBuffer(commands);
                context.Submit();
            }
        }
    }
}
public sealed class ResetProbe : Behaviour
{
    private int frames;
    public override void Update()
    {
        if (++frames == 2) RenderPipelineManager.SetPipelineAsset(null);
        if (frames == 3)
        {
            if (RenderPipelineManager.ActiveAsset != null || RenderPipelineManager.ActivePipeline is not UniversalRenderPipeline)
                throw new InvalidOperationException("Default Universal was not restored.");
            Debug.Log("DEFAULT_UNIVERSAL_RESTORED");
        }
    }
}
''', encoding='utf-8')
    run([args.importer, *imports])
    manifest = read_json(project / 'Library/ScriptBuild/Manifests/Game.json')
    catalog = read_json(inside(project / 'Library' / manifest['TypeCatalog'][4:], project / 'Library'))
    custom_type = next(item for item in catalog['Scripts'] if item['ClrTypeName'].startswith('RetirementTest.CustomAsset,'))
    probe_type = next(item for item in catalog['Scripts'] if item['ClrTypeName'].startswith('RetirementTest.ResetProbe,'))
    custom_guid = str(uuid.uuid4())
    custom = assets / 'Custom.ursaasset'
    write_json(custom, {'format': 'UrsaScriptableAsset', 'version': 1, 'localFileId': 1,
                        'typeId': custom_type['ScriptType'], 'typeVersion': custom_type['Version'],
                        'payload': {'FormatVersion': 1, 'ScriptType': custom_type['ScriptType'],
                                    'Version': custom_type['Version'], 'SourceGuid': custom_type['SourceGuid'],
                                    'LocalFileId': custom_type['LocalFileId'], 'Fields': {}, 'FieldTypes': {}}})
    write_json(Path(str(custom) + '.meta'), {'version': 1, 'guid': custom_guid, 'folder': False,
                                           'importer': '', 'importerVersion': 0, 'settings': {}})
    write_json(graphics_path, {'format': 'UrsaGraphicsSettings', 'version': 1,
                               'renderPipeline': {'guid': custom_guid, 'localFileId': 1}})
    probe = copy.deepcopy(original['gameObjects'][-1])
    probe['id'], probe['name'] = str(uuid.uuid4()), 'Default Reset Probe'
    component = probe['components'][0]
    component['id'] = str(uuid.uuid4())
    component['data'] = {'typeName': probe_type['ClrTypeName'], 'scriptPayload': {
        'FormatVersion': 1, 'ScriptType': probe_type['ScriptType'], 'Version': probe_type['Version'],
        'SourceGuid': probe_type['SourceGuid'], 'LocalFileId': probe_type['LocalFileId'], 'Fields': {}}}
    scene['gameObjects'].append(probe)
    write_json(scene_path, scene)
    run([args.importer, *imports])
    custom_dir, custom_exe, custom_pak = deploy('CustomRuntime')
    for package, label in ((None, 'Loose'), (custom_pak, 'Pak')):
        capture = custom_dir / (label + '.png')
        output = play(custom_dir, custom_exe, package, capture=capture)
        expect('DEFAULT_UNIVERSAL_RESTORED' in output, f'Runtime reset did not finish: {output}')
        expect(sha256(capture) == sha256(baseline), 'Custom-to-default reset changed the equivalent final output')
    for deployed, executable, relative, diagnostics in (
        (default_dir, default_exe, 'Rendering/DefaultUniversalPipeline.ursaasset',
         ('DefaultUniversalPipeline', '8fa13d6e')),
        (custom_dir, custom_exe, 'Rendering/DefaultUniversalRenderer.ursaasset',
         ('DefaultUniversalRenderer', 'c51366e1')),
        (default_dir, default_exe, 'Shaders/Internal/FinalPost.ursashaderasset',
         ('FinalPost', '10000000-0000-4000-8000-000000000004'))):
        dependency = inside(deployed / 'Content/Engine' / relative, deployed)
        saved = dependency.read_bytes()
        dependency.unlink()
        try:
            failure = play(deployed, executable, expected=1)
            expect(any(text in failure for text in diagnostics),
                   f'Missing default dependency {relative} was not diagnosed: {failure}')
        finally:
            dependency.write_bytes(saved)
    print(f'Default Universal bootstrap/empty content, retired asset, standalone loose/Pak and custom reset passed: {root}')


if __name__ == '__main__':
    main_entry(main)
