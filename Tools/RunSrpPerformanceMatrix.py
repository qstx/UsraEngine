"""Capture sequential Legacy/SRP performance runs with independent evidence directories."""
import argparse
from pathlib import Path
import sys
from AutomationSupport import ENGINE, expect, run
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name, default in (('legacy-build', 'SrpLegacyPerformance'), ('srp-build', 'SrpPerformance'),
                          ('legacy-project', 'SrpLegacyProject'), ('srp-project', 'SrpPerformanceProject'),
                          ('output-root', 'SrpPerformanceEvidence/Formal')):
        parser.add_argument('--' + name, type=Path, default=ENGINE / 'Build' / default)
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--frames', type=int, default=1320)
    parser.add_argument('--warmup', type=int, default=120)
    parser.add_argument('--samples', type=int, default=900)
    args = parser.parse_args()
    expect(1 <= args.rounds <= 20 and 240 <= args.frames <= 100000 and 1 <= args.warmup <= 1000 and
           20 <= args.samples <= 10000, 'Performance capture parameters are outside supported ranges.')
    expect(args.frames > args.warmup + args.samples, 'Frames must exceed warmup plus samples.')
    output = args.output_root.resolve()
    expect(not output.exists(), f'Output already exists; choose a fresh evidence directory: {output}')
    output.mkdir(parents=True)
    for iteration in range(1, args.rounds + 1):
        for name, scene in (('Snake', 'Snake/Snake.ursascene'), ('Materials', 'MaterialShowcase.ursascene')):
            for variant, build, project in (('Legacy', args.legacy_build, args.legacy_project), ('SRP', args.srp_build, args.srp_project)):
                player = build.resolve() / 'Release/UrsaScriptPlayer.exe'
                expect(player.is_file() and (project / 'Assets').is_dir(), f'Missing built Player or prepared project for {variant}.')
                print(run([sys.executable, ENGINE / 'Tools/CaptureRenderingPerformance.py', '--player', player,
                           '--project', project.resolve(), '--scene', scene, '--frames', args.frames,
                           '--warmup', args.warmup, '--samples', args.samples, '--output', output / f'{variant}-{name}-{iteration}']))
    print(f'Completed {args.rounds * 4} sequential captures under {output}.')


if __name__ == '__main__':
    main_entry(main)
