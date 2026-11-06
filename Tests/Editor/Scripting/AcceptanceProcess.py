"""Track only IDE processes explicitly created by the acceptance run."""
import re
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'Tools'))
from WindowsAutomation import Process, stop_process


def started_ide(output):
    match = re.search(r'Started Visual Studio PID (\d+)', output)
    if match:
        try:
            return Process(int(match[1]))
        except ProcessLookupError:
            pass
    return None
