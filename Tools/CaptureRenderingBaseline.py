"""Capture rendering baselines and record binary, asset and build identities."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
from AutomationSupport import ENGINE, environment, expect, read_json, run, run_logged, sha256, write_json
from WorkspaceCommon import main_entry


CAPTURES = [
    ('SnakePlayer', False, ['--scene', 'Snake/Snake.ursascene']),
    ('MaterialsPlayer', False, ['--scene', 'MaterialShowcase.ursascene']),
    ('SnakeScene', True, ['--scene', 'Snake/Snake.ursascene', '--play', '--view', 'Scene']),
    ('SnakeGame', True, ['--scene', 'Snake/Snake.ursascene', '--play', '--view', 'Game']),
    ('MaterialsScene', True, ['--scene', 'MaterialShowcase.ursascene', '--play', '--view', 'Scene']),
    ('MaterialsGame', True, ['--scene', 'MaterialShowcase.ursascene', '--play', '--view', 'Game']),
    ('GoldPreview', True, ['--asset', 'Materials/Gold.ursamat', '--view', 'Material Preview'])]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ENGINE / 'Build')
    parser.add_argument('--project-dir', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--configuration', choices=('Debug', 'Release', 'Publish'), default='Debug')
    parser.add_argument('--manifest-only', action='store_true')
    parser.add_argument('--capture-names', nargs='+', choices=[item[0] for item in CAPTURES])
    args = parser.parse_args()
    build = args.build_dir.resolve()
    project = (args.project_dir or build / 'TestProjects/Sample' / args.configuration).resolve()
    output = (args.output_dir or build / 'SrpBaseline' / datetime.now().strftime('%Y%m%d-%H%M%S')).resolve()
    expect(args.manifest_only or not output.exists(), 'Choose a new output directory to preserve earlier evidence.')
    expect(not (output / 'Manifest.json').exists(), 'A manifest already exists; choose a new evidence directory.')
    player, editor = build / args.configuration / 'UrsaScriptPlayer.exe', build / 'Editor' / args.configuration / 'UrsaEditor.exe'
    for executable in (player, editor):
        expect(executable.is_file(), f'Missing executable: {executable}')
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for name, use_editor, extra in CAPTURES:
        if args.capture_names and name not in args.capture_names:
            continue
        executable, png, user = editor if use_editor else player, output / f'{name}.png', output / 'User' / name
        arguments = ['--project', str(project), *extra, '--hidden', '--frames', '120', '--capture', str(png), '--capture-frame', '120']
        with environment(URSA_USER_DATA_ROOT=user):
            if not args.manifest_only:
                run_logged([executable, *arguments], output / f'{name}.out.log', output / f'{name}.log')
        expect(png.is_file(), f'Missing capture: {png}')
        records.append(dict(name=name, executable=str(executable), arguments=arguments, userRoot=str(user),
                            imageSha256=sha256(png), imageCreatedAtUtc=datetime.fromtimestamp(png.stat().st_mtime, timezone.utc).isoformat()))
        print(f'Captured {name}')
    assets = []
    for root in (project / 'Assets', ENGINE / 'Assets'):
        for meta in sorted(root.rglob('*.meta')):
            metadata = read_json(meta)
            if metadata.get('folder'):
                continue
            source = meta.with_suffix('')
            assets.append(dict(path=str(source), guid=metadata.get('guid'), sourceSha256=sha256(source), metaSha256=sha256(meta)))
    manifest = dict(recordedAtUtc=datetime.now(timezone.utc).isoformat(), configuration=args.configuration,
                    engineRevision=run(['git', '-C', ENGINE, 'rev-parse', 'HEAD']).strip(),
                    projectRevision=run(['git', '-C', project, 'rev-parse', 'HEAD']).strip(),
                    engineStatus=run(['git', '-C', ENGINE, 'status', '--short']).splitlines(),
                    cmakeOptions=[line.strip() for line in (build / 'CMakeCache.txt').read_text(encoding='utf-8-sig').splitlines() if line.startswith(('URSA_', 'CMAKE_GENERATOR:'))],
                    binaries=[dict(path=str(p), sha256=sha256(p)) for p in (player, editor)],
                    captureFrame=120, playerResolution=[1024, 720], editorResolution=[1280, 720],
                    vsync='ON / Vulkan FIFO', performanceWarmupFrames=120, performanceSampleFrames=900,
                    performanceRepeats=3, captures=records, assets=assets)
    write_json(output / 'Manifest.json', manifest)
    print(f'Evidence: {output}')


if __name__ == '__main__':
    main_entry(main)
