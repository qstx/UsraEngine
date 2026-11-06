"""Verify IDE refresh, asset identity, reconstruction and failure recovery."""
import argparse
from pathlib import Path
import sys
import uuid
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Tools'))
from AutomationSupport import expect, read_json, remove_tree, run, sha256, write_json
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    args = parser.parse_args()
    build = args.build_directory.resolve()
    fixture = build / 'Temp' / ('UnifiedRefresh-' + uuid.uuid4().hex)
    assets = fixture / 'Assets'
    assets.mkdir(parents=True)
    description = read_json(build / 'UnifiedWorkspace.json')
    description.update(ProjectRoot=fixture.as_posix(), Ready=True)
    descriptor = fixture / 'Workspace.json'
    write_json(descriptor, description)
    def generate():
        run([args.compiler, fixture, '--ide', '--workspace', descriptor])
    source = assets / 'Probe.cs'
    source.write_text('class Probe : MissingType { this is invalid; }', encoding='utf-8')
    generate()
    library = fixture / 'Library'
    game, editor = [library / f'ScriptIDE/Projects/{group}/{name}.csproj' for group, name in (('Game', 'Ursa.Game'), ('Editor', 'Ursa.EditorScripts'))]
    expect('Probe.cs' in game.read_text(encoding='utf-8-sig'), 'Invalid source omitted')
    expect(not (library / 'ScriptBuild').exists(), 'IDE generation published formal output')
    timestamp, metadata = game.stat().st_mtime_ns, sha256(str(source) + '.meta')
    generate()
    expect(game.stat().st_mtime_ns == timestamp, 'No-op generation rewrote project')
    moved = assets / 'Editor/Moved.cs'
    moved.parent.mkdir()
    source.rename(moved); Path(str(source) + '.meta').rename(str(moved) + '.meta')
    generate()
    expect(sha256(str(moved) + '.meta') == metadata, 'Movement changed script metadata')
    expect('Probe.cs' not in game.read_text(encoding='utf-8-sig') and 'Moved.cs' in editor.read_text(encoding='utf-8-sig'), 'Cross-group movement retained stale members')
    moved.unlink(); Path(str(moved) + '.meta').unlink()
    generate()
    expect('Moved.cs' not in editor.read_text(encoding='utf-8-sig'), 'Deleted source retained')
    remove_tree(library, fixture)
    generate()
    expect((library / 'ScriptIDE/UnifiedWorkspace.json').exists(), 'Library reconstruction lost association')
    run([args.compiler, fixture, '--ide', '--workspace', fixture / 'MissingWorkspace.json'], expected=1)
    generate()
    print('Fresh Library, error scripts, no-op refresh, cross-group movement, deletion and failure recovery passed.')


if __name__ == '__main__':
    main_entry(main)
