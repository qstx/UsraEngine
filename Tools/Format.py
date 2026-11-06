"""Format first-party C++ with clang-format 19; optionally check project C# whitespace."""
import argparse
import shutil
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
from AutomationSupport import ENGINE, expect
from WorkspaceCommon import main_entry


def find_formatter(explicit=None):
    candidate = explicit or shutil.which('clang-format')
    if not candidate:
        import os
        locator = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        expect(locator.is_file(), 'clang-format was not found. Pass --clang-format-path.')
        installation = subprocess.check_output([str(locator), '-latest', '-products', '*', '-property', 'installationPath'], text=True).strip()
        candidate = Path(installation) / 'VC/Tools/Llvm/x64/bin/clang-format.exe'
    version = subprocess.check_output([str(candidate), '--version'], text=True)
    expect('clang-format version 19.' in version, 'This repository uses clang-format 19. Pass --clang-format-path.')
    return str(candidate)


def differs(formatter, path):
    replacements = ET.fromstring(subprocess.check_output([formatter, '--style=file', '--output-replacements-xml', str(path)]))
    source = path.read_bytes()
    return any(source[int(node.attrib['offset']):int(node.attrib['offset']) + int(node.attrib['length'])] !=
               (node.text or '').encode('utf-8') for node in replacements.findall('replacement'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--clang-format-path')
    parser.add_argument('--project-dir', type=Path)
    args = parser.parse_args()
    formatter = find_formatter(args.clang_format_path)
    files = sorted(p for module in ('Runtime', 'Engine', 'Editor', 'Tests') for p in (ENGINE / module).rglob('*')
                   if p.suffix in ('.cpp', '.hpp', '.h', '.c') and p.is_file()
                   and 'Scripting/Bindings/Generated/' not in p.as_posix())
    failed = []
    for path in files:
        if args.check:
            if differs(formatter, path):
                print(f'Formatting differs: {path}')
                failed.append(path)
        else:
            subprocess.run([formatter, '-i', '--style=file', str(path)], check=True)
    if args.project_dir:
        for folder in (ENGINE / 'Tests/Managed/Ursa.SnakeLogicTests', args.project_dir.resolve() / 'Assets/Scripts'):
            command = ['dotnet', 'format', 'whitespace', str(folder), '--folder']
            if args.check:
                command.append('--verify-no-changes')
            subprocess.run(command, check=True)
    expect(not failed, f'clang-format failed for {len(failed)} file(s).')
    print(f'{"Checked" if args.check else "Formatted"} {len(files)} Engine C++ files.')


if __name__ == '__main__':
    main_entry(main)
