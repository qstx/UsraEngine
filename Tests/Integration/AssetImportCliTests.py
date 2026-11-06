"""Exercise import scope/cache, writer locks, recovery and deterministic content packaging."""
import argparse
from pathlib import Path
import re
import shutil
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Tools'))
from AutomationSupport import expect, inside, read_json, run, sha256
from WindowsAutomation import exclusive_file
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('importer', 'player', 'package-tool', 'project', 'source'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--configuration', required=True)
    args = parser.parse_args()
    project = args.project.resolve()
    assets, library = project / 'Assets', project / 'Library'
    shutil.copytree(args.source / 'Assets', assets, dirs_exist_ok=True)
    imports = ['--project', project, '--configuration', args.configuration]
    def invoke(executable, arguments, expected=0):
        return run([executable, *arguments], expected=expected)
    def library_state():
        return [(str(p), p.stat().st_size, p.stat().st_mtime_ns, sha256(p)) for p in sorted(library.rglob('*')) if p.is_file()]
    invoke(args.importer, imports)
    warm = invoke(args.importer, imports)
    expect('Editor: imported=0' in warm and 'Player: imported=0' in warm, f'Repeated import missed cache: {warm}')
    invoke(args.importer, imports + ['--scope', 'prj:Textures/UrsaEngine.png', '--force'])
    ready_path = library / f'RuntimeContent/WindowsVulkan/{args.configuration}/Player/Ready.json'
    ready = ready_path.read_bytes()
    invoke(args.importer, imports + ['--target', 'Unsupported'], 1)
    expect(ready_path.read_bytes() == ready, 'Failed target replaced complete content')
    invoke(args.importer, ['--unknown'], 2)
    with exclusive_file(library / 'Import.lock', write=True):
        busy = invoke(args.importer, imports, 1)
        expect(re.search('busy|locked', busy, re.I), f'Missing writer conflict diagnostic: {busy}')
    before = library_state()
    invoke(args.player, ['--project', project, '--scene', 'Snake/Snake.ursascene', '--hidden', '--frames', '3'])
    expect(library_state() == before, 'Loose Player wrote project Library')
    shader = assets / 'Materials/Gold.ursamat'
    original = shader.read_bytes()
    try:
        shader.write_bytes(original + b'\n ')
        stale = invoke(args.player, ['--project', project, '--hidden', '--frames', '1'], 1)
        expect('UrsaAssetImport' in stale, f'Missing stale-content repair instruction: {stale}')
        invoke(args.importer, imports)
    finally:
        shader.write_bytes(original)
    invoke(args.importer, imports)
    entry = next(e for e in read_json(ready_path)['entries'] if e['path'] == 'prj:Textures/UrsaEngine.ursatex')
    artifact = inside(library / entry['storage'][4:], library)
    artifact.unlink()
    invoke(args.player, ['--project', project, '--hidden', '--frames', '1'], 1)
    invoke(args.importer, imports)
    expect(artifact.exists(), 'Missing output did not recover')
    package = project / 'Content.pak'
    package_args = ['--project-root', project, '--configuration', args.configuration, '--output', package]
    invoke(args.package_tool, package_args)
    first_hash = sha256(package)
    invoke(args.package_tool, package_args)
    expect(sha256(package) == first_hash, 'Current content packaging was not deterministic')
    invalid = assets / 'Invalid.ursashader'
    invalid.write_text('invalid shader source', encoding='utf-8')
    try:
        invoke(args.package_tool, package_args, 1)
        expect(sha256(package) == first_hash, 'Failed import replaced complete pak')
    finally:
        invalid.unlink(missing_ok=True)
        Path(str(invalid) + '.meta').unlink(missing_ok=True)
    invoke(args.player, ['--pak', package, '--scene', 'Snake/Snake.ursascene', '--hidden', '--frames', '3'])
    print('CLI scope/cache/errors/writer lock/recovery, read-only Player and deterministic content build passed')


if __name__ == '__main__':
    main_entry(main)
