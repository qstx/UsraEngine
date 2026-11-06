"""Verify cold F5 hits RenderInternal before entering Play in an isolated solution."""
import argparse
import json
from pathlib import Path
import subprocess
import time
import uuid
import xml.etree.ElementTree as ET
from AcceptanceProcess import stop_process
from AutomationSupport import environment, expect, hidden_options, read_json, sha256, wait_until, write_json
from WindowsAutomation import Process
from VisualStudioSupport import Controller, build_controller, source_line
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', type=Path, required=True)
    parser.add_argument('--visual-studio-install-directory', type=Path, required=True)
    parser.add_argument('--configuration', choices=('Debug', 'Release'), default='Debug')
    args = parser.parse_args()
    build, vs = args.build_directory.resolve(), args.visual_studio_install_directory.resolve()
    workspace = read_json(build / 'UnifiedWorkspace.json')
    expect(workspace['Ready'], 'Generate a ready unified workspace before F5 acceptance.')
    source = Path(workspace['EngineRoot']) / 'Managed/Ursa.Rendering/RenderPipeline.cs'
    line = source_line(source, 'ObjectDisposedException.ThrowIf(disposed, this);')
    root = build / 'IdeAcceptance' / ('PrePlayLaunch-' + uuid.uuid4().hex)
    root.mkdir(parents=True)
    xml = ET.parse(build / 'UrsaEngine.slnx')
    for node in xml.iter():
        attribute = 'Path' if node.tag == 'Project' else 'Project' if node.tag == 'BuildDependency' else None
        if attribute and attribute in node.attrib:
            node.set(attribute, (build / node.attrib[attribute]).resolve().as_posix())
    solution = root / 'UrsaEngine.slnx'
    xml.write(solution, encoding='utf-8')
    temporary = build / 'Temp'
    temporary.mkdir(exist_ok=True)
    devenv, editor_exe = vs / 'Common7/IDE/devenv.exe', build / f'Editor/{args.configuration}/UrsaEditor.exe'
    owned, editor, launch, created = None, None, None, False
    with environment(URSA_USER_DATA_ROOT=root / 'User', TEMP=temporary, TMP=temporary):
        controller = Controller(build_controller(build, vs), solution, root, timeout=60)
        try:
            expect(editor_exe.is_file(), f'Build UrsaEditor/{args.configuration} before this acceptance.')
            owned = subprocess.Popen([str(devenv), str(solution)], **hidden_options())
            def loaded():
                try:
                    controller('projects')
                    return True
                except RuntimeError:
                    return False
            wait_until(loaded, 120, 'Visual Studio solution load', .3)
            launch = controller('launch', 'UrsaEditor', args.configuration)
            editor = Process(launch['Pid'])
            log = root / 'User/UrsaEditor/Logs/UrsaEditor.log'
            def ready():
                expect(editor.poll() is None, 'Editor exited before managed startup')
                return log.exists() and '[Script] Ursa 托管运行时已初始化' in log.read_text(encoding='utf-8-sig')
            wait_until(ready, 90, 'main-thread managed startup', .25)
            membership = controller('project-file', source)
            created = controller('breakpoint', source, line)['Created']
            def hit():
                state = controller('state')
                return state if state['Mode'] == 2 and state['Function'].startswith('Ursa.Rendering.RenderPipeline.RenderInternal') else None
            state = wait_until(hit, 25, 'RenderInternal before Play')
            passed = state['Line'] == line and state['Project'].lower().endswith('.csproj')
            evidence = dict(Launch=launch, State=state, PlayRequested=False, HostLog=str(log), Membership=membership, Passed=passed)
            if passed:
                camera, location = controller('evaluate', 'cameras.Count'), controller('evaluate', 'typeof(Ursa.Rendering.RenderPipeline).Assembly.Location')
                expect(camera['Valid'] and camera['Value'] == '1' and location['Valid'], 'Cannot inspect camera and framework module')
                loaded_hash = sha256(json.loads(location['Value']))
                formal_hash = sha256(build / f'Managed/{args.configuration}/Ursa.Rendering.dll')
                evidence.update(CameraCount=camera, FrameworkLocation=location, FrameworkSha256=loaded_hash, FormalFrameworkSha256=formal_hash, Passed=loaded_hash == formal_hash)
            write_json(root / 'Evidence.json', evidence)
            expect(evidence['Passed'], 'Cold F5 did not hit RenderInternal before Play')
            print('Cold F5: RenderInternal breakpoint added after startup hit before Play.')
        finally:
            try:
                if owned:
                    if created:
                        try: controller('clear', source, line)
                        except RuntimeError as error: print(error, file=__import__('sys').stderr)
                    if launch:
                        for action, extra in (('stop', []), ('startup', [launch['PreviousStartup']])):
                            try: controller(action, *extra)
                            except RuntimeError as error: print(error, file=__import__('sys').stderr)
            finally:
                try: stop_process(editor, editor_exe)
                finally:
                    stop_process(owned, devenv)
                    print(f'Evidence directory: {root}')


if __name__ == '__main__':
    main_entry(main)
