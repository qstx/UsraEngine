"""Python orchestration for the existing C# Visual Studio automation controller."""
import json
from pathlib import Path
import re
import subprocess
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'Tools'))
from AutomationSupport import expect, hidden_options, run, write_json


BUSY = re.compile(r'RPC_E_CALL_REJECTED|RPC_E_SERVERCALL_RETRYLATER|0x80010001|0x8001010A|No Visual Studio instance')


def build_controller(build, vs):
    root = build / 'IdeAcceptance/VisualStudioController'
    print(run(['dotnet', 'build', Path(__file__).parent / 'VisualStudioAcceptance/VisualStudioAcceptance.csproj',
               '-c', 'Release', '--nologo', '--disable-build-servers', '-m:1', '-nr:false',
               f'-p:VisualStudioInstallDirectory={vs}', f'-p:UrsaAcceptanceBuildRoot={root}']))
    return root / 'bin/Release/net10.0-windows/VisualStudioAcceptance.dll'


class Controller:
    def __init__(self, executable, solution, logs, timeout=30):
        self.executable, self.solution, self.logs = executable, solution, logs
        self.timeout = timeout
        logs.mkdir(parents=True, exist_ok=True)

    def raw(self, action, *extra, language=False):
        attempts = 40 if language else 20
        for attempt in range(attempts):
            try:
                result = subprocess.run(list(map(str, ['dotnet', self.executable, action, self.solution, *extra])),
                                        capture_output=True, text=True, encoding='utf-8', errors='replace',
                                        timeout=max(self.timeout, 60) if action in ('attach', 'detach', 'launch') else self.timeout,
                                        **hidden_options())
            except subprocess.TimeoutExpired as error:
                raise RuntimeError(f'Controller timed out: {action}') from error
            (self.logs / 'controller-output.log').write_text(result.stdout, encoding='utf-8')
            (self.logs / 'controller-error.log').write_text(result.stderr, encoding='utf-8')
            if result.returncode == 0:
                return result.stdout
            loading = language and attempt < 3 and re.search('IDE did not complete member|IDE did not navigate', result.stderr)
            expect(loading or BUSY.search(result.stderr), result.stderr)
            time.sleep(.25 if language else .15)
        raise RuntimeError(f'IDE remained busy: {action}: {result.stderr}')

    def __call__(self, action, *extra, language=False):
        text = self.raw(action, *extra, language=language)
        if text.lstrip().startswith(('{', '[')):
            return json.loads(text)
        return text.strip() or None


def start_logged(command, output, errors):
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('wb') as stdout, errors.open('wb') as stderr:
        return subprocess.Popen(list(map(str, command)), stdout=stdout, stderr=stderr, **hidden_options())


def source_line(path, text):
    matches = [index for index, line in enumerate(Path(path).read_text(encoding='utf-8-sig').splitlines(), 1) if text in line]
    expect(len(matches) == 1, f'Expected exactly one source anchor {text} in {path}')
    return matches[0]
