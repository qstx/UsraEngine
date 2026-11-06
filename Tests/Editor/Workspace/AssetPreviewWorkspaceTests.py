"""Verify preview workspace persistence across independent Editor processes."""
import argparse
from pathlib import Path
import sys
import uuid
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'Tools'))
from AutomationSupport import run
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--evidence-root', type=Path, required=True)
    args = parser.parse_args()
    for variant in ('Loose', 'Pak'):
        project = args.evidence_root.resolve() / variant / uuid.uuid4().hex[:12]
        for mode in range(4):
            print(run([args.executable, project, variant, mode]))


if __name__ == '__main__':
    main_entry(main)
