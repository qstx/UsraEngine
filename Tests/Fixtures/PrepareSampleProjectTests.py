"""Verify isolated Sample preparation without access to a Demo project."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import uuid


def snapshot(folder):
    return {p.relative_to(folder).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in folder.rglob('*') if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', required=True)
    parser.add_argument('--build-root', type=Path, required=True)
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    build = args.build_root.resolve()
    project = build / 'TestProjects/Sample' / ('Prepare-' + uuid.uuid4().hex)

    def prepare(target):
        return subprocess.run([args.cmake, f'-DBUILD_ROOT={build}', f'-DPROJECT={target}',
                               f'-DSOURCE={build}/UnavailableOldDemo/Assets', '-DPREPARE_ONLY=ON',
                               '-P', str(fixture / 'PrepareSampleProject.cmake')],
                              capture_output=True, text=True, encoding='utf-8', errors='replace')

    expected = snapshot(fixture / 'SampleAssets')
    result = prepare(project)
    assert result.returncode == 0, result.stdout + result.stderr
    assert snapshot(project / 'Assets') == expected, 'Cold preparation omitted Engine fixture content'
    (project / 'Assets/stale.txt').write_text('stale', encoding='utf-8')
    (project / 'Library').mkdir()
    (project / 'Library/stale.txt').write_text('stale', encoding='utf-8')
    result = prepare(project)
    assert result.returncode == 0, result.stdout + result.stderr
    assert snapshot(project / 'Assets') == expected and not (project / 'Library').exists(), 'Repeated preparation retained stale files'
    outside = build / 'Temp/SamplePreparationGuard'
    outside.mkdir(parents=True, exist_ok=True)
    marker = outside / 'Keep.txt'
    marker.write_text('preserve', encoding='utf-8')
    result = prepare(outside)
    assert result.returncode != 0 and marker.read_text(encoding='utf-8') == 'preserve', 'Preparation escaped the approved test path'
    print(f'Engine-only Sample preparation verified: {len(expected)} files, cold/repeated copies, stale cleanup and path guard.')


if __name__ == '__main__':
    main()
