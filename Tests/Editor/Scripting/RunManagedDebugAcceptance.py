"""Verify managed/native breakpoints, stepping, locals and attached script reload."""
import argparse
import fnmatch
from pathlib import Path
import re
import sys
import time
import uuid
from AcceptanceProcess import started_ide, stop_process
from AutomationSupport import environment, expect, read_json, sha256, wait_until, write_json
from GenerateUnifiedIdeFixture import generate
from VisualStudioSupport import Controller, build_controller, source_line, start_logged
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', type=Path, required=True)
    parser.add_argument('--visual-studio-install-directory', type=Path, required=True)
    parser.add_argument('--configuration', default='Debug', choices=('Debug', 'Release'))
    parser.add_argument('--unified', action='store_true')
    parser.add_argument('--framework-prefix', default='')
    parser.add_argument('--startup-timeout', type=int, default=240, help='cold-import startup deadline in seconds')
    args = parser.parse_args()
    build, vs = args.build_directory.resolve(), args.visual_studio_install_directory.resolve()
    if args.unified:
        expect(re.search(r'^URSA_PROJECT_DIR:PATH=.+$', (build / 'CMakeCache.txt').read_text(encoding='utf-8-sig'), re.M), 'Unified debug acceptance requires an existing project association.')
    acceptance = build / 'IdeAcceptance'
    root = acceptance / ('DebugRun-' + uuid.uuid4().hex[:12])
    controller_exe = build_controller(build, vs)
    solution = root / 'Library/ScriptIDE/Ursa.Scripts.sln'
    state_path, command_path = [root / f'Library/IdeAcceptance/{name}.json' for name in ('State', 'Command')]
    game, editor = root / 'Assets/DebugProbe.cs', root / 'Assets/Editor/EditorProbe.cs'
    records, breakpoints = [], []
    sequence = 0
    host = owned = None
    attached = False
    navigation_output = acceptance / 'debug-navigation-output.log'
    navigation_started = False
    devenv, host_exe = vs / 'Common7/IDE/devenv.exe', build / args.configuration / 'UrsaManagedDebugAcceptance.exe'
    controller = Controller(controller_exe, solution, acceptance)
    def state():
        return read_json(state_path)
    def send(action):
        nonlocal sequence
        sequence += 1
        write_json(command_path, dict(Sequence=sequence, Action=action))
    def add_breakpoint(file, line):
        if controller('breakpoint', file, line)['Created']:
            breakpoints.extend((str(file), str(line)))
    def verify(function, line, variable, expected, remove=False):
        def hit(step=False):
            capture = controller('state')
            return capture if capture['Mode'] == 2 and fnmatch.fnmatchcase(capture['Function'], function) and (capture['Line'] > line if step else capture['Line'] == line) else None
        before = wait_until(hit, 25, f'breakpoint {function}')
        if function.startswith(('DebugAcceptance.', 'Ursa.')):
            expect(before['Project'].lower().endswith('.csproj'), f'Breakpoint outside C# project: {before["File"]}')
        controller('step')
        after = wait_until(lambda: hit(True), 15, f'single step {function}')
        value = controller('evaluate', variable)
        if not value['Valid'] or value['Value'] != expected:
            controller('continue')
            raise RuntimeError(f'Unexpected local {variable}: {value}')
        records.append(dict(Break=before, Step=after, Local=value))
        print(f'{function}: {variable} = {expected}')
        if remove:
            controller('clear', before['File'], line)
        controller('continue')
    temporary = build / 'Temp'
    temporary.mkdir(exist_ok=True)
    with environment(URSA_USER_DATA_ROOT=acceptance / 'User', TEMP=temporary, TMP=temporary):
        try:
            host = start_logged([host_exe, root], acceptance / 'controlled-host.log', acceptance / 'controlled-host-error.log')
            def ready():
                expect(host.poll() is None, 'Controlled host exited during startup; inspect controlled-host-error.log')
                return state_path.exists() and state()['Ready'] and not state()['Busy']
            wait_until(ready, args.startup_timeout, 'initial editing session')
            if args.unified:
                controller.solution = generate(build, root)
            navigation = start_logged([build / f'Editor/{args.configuration}/UrsaIdeBridge.exe', devenv, controller.solution, game, 11, 5], navigation_output, acceptance / 'debug-navigation-error.log')
            navigation_started = True
            try:
                status = navigation.wait(timeout=90)
            except BaseException:
                navigation.kill(); navigation.wait()
                raise
            owned = started_ide(navigation_output.read_text(encoding='utf-8-sig'))
            expect(status == 0, 'IDE could not open controlled solution')
            ide = controller('info')
            write_json(acceptance / 'MixedIdeInfo.json', ide)
            members = [controller('project-file', path) for path in (game, editor)]
            write_json(acceptance / 'MixedProjectMembership.json', members)
            add_breakpoint(game, 11); add_breakpoint(editor, 9)
            engine = '{2E36F1D4-B23C-435D-AB41-18E608940038}'
            if args.unified:
                engine += ';{3B476D35-A401-11D2-AAD4-00C04F990171}'
                native = Path(__file__).with_name('ManagedDebugAcceptance.cpp')
                native_line = source_line(native, 'const Int32 nativeMarker = seed + 7;')
                add_breakpoint(native, native_line)
            attached = True
            controller('attach', host.pid, engine)
            if args.unified:
                send('native'); verify('*NativeDebugProbe*', native_line, 'nativeMarker', '42')
            pre_play = state()
            expect(not pre_play['Running'], 'Render breakpoint acceptance already entered Play')
            if args.unified:
                render = Path(__file__).resolve().parents[3] / 'Managed/Ursa.Rendering/RenderPipeline.cs'
                line = source_line(render, 'ObjectDisposedException.ThrowIf(disposed, this);')
                controller('project-file', render); add_breakpoint(render, line)
                verify('Ursa.Rendering.RenderPipeline.RenderInternal*', line, 'cameras.Count', '1', remove=True)
            send('menu'); verify('DebugAcceptance.EditorProbe.Pulse', 9, 'editorMarker', '41')
            send('play'); verify('DebugAcceptance.DebugProbe.Awake', 11, 'runtimeMarker', '10')
            wait_until(lambda: state()['RunningName'] == args.framework_prefix + 'Debug:10', 10, 'first runtime callback')
            def detach():
                controller('detach')
                wait_until(lambda: (lambda s: s['Mode'] == 1 and not s['Processes'])(controller('state')), 30, 'debugger detach completion')
            if not args.unified:
                detach(); attached = False
            manifest = root / 'Library/ScriptBuild/Manifests/Game.json'
            symbols = root / 'Library/ScriptBuild/Assemblies/Game/Ursa.Game.pdb'
            old_manifest, old_session, old_symbols = read_json(manifest), state()['DebugInfo'], sha256(symbols)
            text = game.read_text(encoding='utf-8-sig')
            expect('CodeVersion = 1' in text, 'Controlled source fixture changed unexpectedly')
            game.write_text(text.replace('CodeVersion = 1', 'CodeVersion = 2'), encoding='utf-8')
            refresh = controller('refresh-source', game)
            time.sleep(3)
            expect(state()['RunningName'] == args.framework_prefix + 'Debug:10', 'Run mixed old and new script versions')
            send('stop')
            def recompiled():
                current, updated = state(), read_json(manifest)
                return not current['Running'] and not current['Busy'] and current['DebugInfo'] != old_session and updated['BuildFingerprint'] != old_manifest['BuildFingerprint']
            wait_until(recompiled, 60, 'formal recompilation and editing reload')
            reloaded = state()
            expect(reloaded['Dirty'] and reloaded['Payload']['Fields']['value'] == 9, 'Reload lost unsaved editing data')
            if str(game) in breakpoints:
                controller('clear', game, 11); controller('breakpoint', game, 11)
            if not args.unified:
                attached = True; controller('attach', host.pid, engine)
            send('menu'); verify('DebugAcceptance.EditorProbe.Pulse', 9, 'editorMarker', '42')
            send('play'); verify('DebugAcceptance.DebugProbe.Awake', 11, 'runtimeMarker', '11')
            wait_until(lambda: state()['RunningName'] == args.framework_prefix + 'Debug:11', 10, 'new runtime callback')
            if not args.unified:
                detach(); attached = False
            send('stop')
            wait_until(lambda: not state()['Running'] and not state()['Busy'], 30, 'final Stop')
            write_json(acceptance / 'managed-debug-evidence.json', dict(Project=str(root), Solution=str(controller.solution), Pid=host.pid, IDE=ide,
                       Records=records, Membership=members, SourceRefresh=refresh, FrameworkPrefix=args.framework_prefix,
                       FrameworkSha256=sha256(build / f'Managed/{args.configuration}/Ursa.Runtime.dll'), ReloadWhileAttached=args.unified,
                       PrePlayRenderState=pre_play, OldBuildFingerprint=old_manifest['BuildFingerprint'], NewBuildFingerprint=read_json(manifest)['BuildFingerprint'],
                       OldPdbSha256=old_symbols, NewPdbSha256=sha256(symbols), ReloadedState=reloaded))
            controller('clear', *breakpoints); breakpoints.clear()
            send('close')
            expect(host.wait(timeout=15) == 0, 'Controlled host failed')
            attached = False
            print('Game and Editor breakpoints, stepping, locals and reloaded-version breakpoints passed.')
        except BaseException as error:
            write_json(acceptance / 'MixedDebugFailure.json', dict(Error=str(error), Records=records,
                       ControllerOutput=(acceptance / 'controller-output.log').read_text(encoding='utf-8') if (acceptance / 'controller-output.log').exists() else '',
                       ControllerError=(acceptance / 'controller-error.log').read_text(encoding='utf-8') if (acceptance / 'controller-error.log').exists() else ''))
            raise
        finally:
            for action, extra in ([('detach', [])] if attached and not args.unified else []) + ([('clear', breakpoints)] if breakpoints else []):
                try: controller(action, *extra)
                except RuntimeError as error: print(error, file=sys.stderr)
            try:
                if host and host.poll() is None:
                    try: send('close'); host.wait(timeout=3)
                    except Exception as error: print(error, file=sys.stderr)
                    stop_process(host, host_exe)
            finally:
                if owned is None and navigation_started and navigation_output.exists():
                    owned = started_ide(navigation_output.read_text(encoding='utf-8-sig'))
                stop_process(owned, devenv)


if __name__ == '__main__':
    main_entry(main)
