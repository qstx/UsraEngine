"""Check production C++ resource access stays behind the directory VFS provider."""
import argparse
import re
from pathlib import Path
from AutomationSupport import ENGINE, expect
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--demo-directory', type=Path)
    args = parser.parse_args()
    roots = [ENGINE / name for name in ('Runtime', 'Engine', 'Editor')]
    if args.demo_directory:
        roots += [args.demo_directory / name for name in ('Snake', 'Editor', 'Material', 'Tools')]
    pattern = re.compile(r'std::(i|o|f)fstream|\b(fopen|fread|fwrite|ReadFile|WriteFile|WriteResourceAtomically)\s*\(|std::filesystem::(remove|rename|copy|create_)|\b(Locate|WritablePath|ResolveToDisk|ResolveForWrite)\s*\(')
    violations = []
    for root in roots:
        for path in root.rglob('*'):
            if path.suffix not in ('.cpp', '.hpp') or path in (ENGINE / 'Runtime/Vfs/DirectoryFileProvider.cpp', ENGINE / 'Runtime/Vfs/DirectoryFileProvider.hpp'):
                continue
            violations += [f'{path}:{index}:{line}' for index, line in enumerate(path.read_text(encoding='utf-8-sig').splitlines(), 1) if pattern.search(line)]
    print('\n'.join(violations))
    expect(not violations, 'Production resource I/O bypasses VFS')
    print('Production resource I/O audit passed; host streams and mutations are confined to DirectoryFileProvider.')


if __name__ == '__main__':
    main_entry(main)
