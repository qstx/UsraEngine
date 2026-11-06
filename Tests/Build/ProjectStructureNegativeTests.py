"""Reject an incorrect filter in an isolated generated-project copy."""
import argparse
from pathlib import Path
import shutil
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Tools'))
from AutomationSupport import expect, inside
from CheckProjectStructure import check
from ReadVisualStudioSolution import read_projects
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--engine-root', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    args = parser.parse_args()
    build, scratch = args.build_dir.resolve(), args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    solution = next(p for p in (build / 'UrsaEngine.slnx', build / 'UrsaEngine.sln') if p.exists())
    for name in ('CMakeCache.txt', solution.name, 'UrsaProjectStructure.json'):
        shutil.copy2(build / name, scratch / name)
    for project in read_projects(build).values():
        path = project['path']
        if path.suffix != '.vcxproj':
            continue
        for suffix in ('', '.filters'):
            source = Path(str(path) + suffix)
            if source.exists():
                target = inside(scratch / (str(path.relative_to(build)) + suffix), scratch)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
    filters = scratch / 'Runtime/UrsaRuntime.vcxproj.filters'
    text = filters.read_text(encoding='utf-8-sig')
    broken = text.replace('<Filter>Runtime\\Core</Filter>', '<Filter>WrongDirectory</Filter>')
    expect(broken != text, 'No filter changed in negative fixture')
    filters.write_text(broken, encoding='utf-8')
    try:
        check(scratch)
        raise AssertionError('Structure check accepted incorrect filter')
    except RuntimeError as error:
        expect(str(error).startswith('Wrong filter'), str(error))
    print('Structure check correctly rejected an incorrect filter in an isolated generated-project copy.')


if __name__ == '__main__':
    main_entry(main)
