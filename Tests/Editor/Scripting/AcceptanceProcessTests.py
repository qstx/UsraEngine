"""Test process ownership, failure cleanup, PID tracking and idempotent cleanup."""
from pathlib import Path
import subprocess
import sys
from AcceptanceProcess import started_ide, stop_process
from AutomationSupport import expect, hidden_options
from WorkspaceCommon import main_entry


def main():
    child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'], **hidden_options())
    try:
        try:
            stop_process(child, Path(__file__).with_name('Unrelated.exe'))
            raise AssertionError('Ownership guard accepted unrelated executable')
        except RuntimeError as error:
            expect('unrelated process' in str(error), str(error))
        expect(child.poll() is None, 'Ownership guard terminated unrelated child')
        identified = started_ide(f'Started Visual Studio PID {child.pid}')
        expect(identified is not None and identified.pid == child.pid, 'Bridge PID tracking failed')
        stop_process(child, sys.executable)
        expect(child.poll() is not None, 'Owned acceptance process survived cleanup')
        stop_process(child, sys.executable)
        expect(started_ide('Using existing Visual Studio') is None, 'Existing IDE marked as owned')
        identified.close()
    finally:
        stop_process(child, sys.executable)
        child.wait()
    print('Acceptance process ownership, failure cleanup and repeated cleanup passed.')


if __name__ == '__main__':
    main_entry(main)
