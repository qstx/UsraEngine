"""Verify VS2026 completion and definition navigation in the unified solution."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
from AcceptanceProcess import started_ide, stop_process
from AutomationSupport import expect, read_json, run, sha256, write_json
from VisualStudioSupport import Controller, build_controller
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', type=Path, required=True)
    parser.add_argument('--visual-studio-install-directory', type=Path, required=True)
    args = parser.parse_args()
    build, vs = args.build_directory.resolve(), args.visual_studio_install_directory.resolve()
    workspace = read_json(build / 'UnifiedWorkspace.json')
    expect(workspace['Ready'], 'Generate a complete unified workspace before IDE acceptance.')
    root = build / 'IdeAcceptance'
    controller = Controller(build_controller(build, vs), workspace['Solution'], root)
    fixture = Path(workspace['EngineRoot']) / 'Tests/Fixtures/SampleAssets'
    game = fixture / 'Scripts/Snake/SnakeController.cs'
    editor = fixture / 'Scripts/Editor/SnakeEditor.cs'
    framework = Path(workspace['EngineRoot']) / 'Managed/Ursa.Runtime/Transform.cs'
    hashes = {p: sha256(p) for p in (game, editor, framework)}
    bridge, devenv = build / 'Editor/Debug/UrsaIdeBridge.exe', vs / 'Common7/IDE/devenv.exe'
    owned, records = [], []
    def navigate(source, line, column):
        import subprocess
        from AutomationSupport import hidden_options
        result = subprocess.run(list(map(str, [bridge, devenv, workspace['Solution'], source, line, column])),
                                capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=90, **hidden_options())
        output = result.stdout + result.stderr
        process = started_ide(output)
        if process:
            owned.append(process)
        expect(result.returncode == 0, f'IDE navigation failed: {output}')
    try:
        navigate(game, 118, 4)
        info = controller('info', language=True)
        expect(int(info['Version'].split('.')[0]) >= 18, 'Acceptance requires VS2026')
        records.append(controller('complete', game, language=True))
        records.append(controller('complete-member', editor, 'EditorGui.PropertyField', 'PropertyFiel', 'PropertyField', language=True))
        records.append(controller('complete-member', framework, 'NativeRuntimeBindings.GetLocalTransform', 'GetLocalTransfor', 'GetLocalTransform', language=True))
        records.append(controller('navigate-symbol', game, Path(workspace['EngineRoot']) / 'Managed/Ursa.Runtime/GameObject.cs', 'Ursa.GameObject', language=True))
        records.append(controller('navigate-symbol', editor, game, 'SnakeController', language=True))
        navigate(editor, 11, 3)
        write_json(root / 'UnifiedLanguageEvidence.json', dict(IDE=info, Solution=workspace['Solution'], Records=records, VerifiedAt=datetime.now(timezone.utc).isoformat()))
    finally:
        try:
            expect(all(sha256(p) == value for p, value in hashes.items()), 'IDE verification changed source on disk')
        finally:
            try:
                if owned:
                    controller('close-fixture-documents', build)
            finally:
                for process in owned:
                    stop_process(process, devenv)
    print('VS2026 completion, framework/Game definition navigation and unified-solution positioning passed.')


if __name__ == '__main__':
    main_entry(main)
