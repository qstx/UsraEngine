"""Exercise capture identity, recovery, window lifecycle and bounded private memory."""
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import subprocess
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Tools'))
from AutomationSupport import expect, hidden_options, read_json, sha256, wait_until, write_json
from WindowsAutomation import Bitmap, api, exclusive_file, find_window, memory, stop_process


class CaptureSamples:
    def __init__(self, capture, project, invoke):
        self.capture, self.project, self.invoke = capture, project, invoke

    def start(self, executable, arguments, name):
        out, err = self.project / f'{name}.stdout.log', self.project / f'{name}.stderr.log'
        with out.open('wb') as stdout, err.open('wb') as stderr:
            process = subprocess.Popen(list(map(str, [executable, *arguments])), stdout=stdout, stderr=stderr, **hidden_options())
        registration = Path(os.environ['URSA_USER_DATA_ROOT']) / f'UrsaCapture/instances/{process.pid}.json'
        def ready():
            expect(process.poll() is None, f'Capture target exited: {name}; inspect {err}')
            return registration.exists() and read_json(registration)['Ready']
        try:
            wait_until(ready, 90, 'capture registration', .02)
        except BaseException:
            stop_process(process, executable)
            raise
        return process

    def request(self, process, output, expected='Success', timeout=10000):
        result = subprocess.run(list(map(str, [self.capture, '--pid', process.pid, '--output', output, '--timeout-ms', timeout])),
                                capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=timeout / 1000 + 15, **hidden_options())
        (self.project / 'Capture.stderr.log').write_text(result.stderr, encoding='utf-8')
        data = json.loads(result.stdout)
        expect(data['Status'] == expected and data['Pid'] == process.pid and (result.returncode == 0) == (expected == 'Success'), f'Unexpected capture result: {result.stdout}\n{result.stderr}')
        suffix = '.result.json' if expected == 'Success' else f'.{expected}.result.json'
        write_json(str(output) + suffix, data)
        if expected == 'Success':
            with Bitmap(output) as bitmap:
                expect(bitmap.size == (data['Width'], data['Height']), 'PNG and result dimensions differ')
        return data

    def test(self, executable, arguments, name, lifecycle=False):
        process, peer = self.start(executable, arguments, name), None
        def request(suffix, expected='Success', timeout=10000, target=None):
            return self.request(target or process, self.project / f'{name}-{suffix}.png', expected, timeout)
        try:
            first, second = request('第一次 截图'), request('Second')
            expect(first['SessionId'] == second['SessionId'] and first['RequestId'] != second['RequestId'] and second['FrameSerial'] > first['FrameSerial'], 'Repeated capture did not produce new images of same instance')
            output = self.project / f'{name}-Second.png'
            before = sha256(output)
            with exclusive_file(output):
                request('Second', 'WriteFailed')
            expect(sha256(output) == before, 'Failed write corrupted existing PNG')
            request('Recovered')
            if lifecycle:
                peer = self.start(executable, arguments, name + '-Peer')
                peer_result = request('Peer', target=peer)
                expect(peer_result['SessionId'] != first['SessionId'], 'Processes shared capture identity')
                window = find_window(process.pid, ('Ursa Script Player', 'Ursa Editor'))
                expect(window, 'Capture target window unavailable')
                expect(api('user32', 'SetWindowPos', W.BOOL, W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT)(window, None, 0, 0, 900, 600, 0x14), 'Cannot resize capture target')
                resized = request('Resized')
                expect((resized['Width'], resized['Height']) != (second['Width'], second['Height']), 'Resize did not change captured dimensions')
                show = api('user32', 'ShowWindow', W.BOOL, W.HWND, C.c_int)
                iconic = api('user32', 'IsIconic', W.BOOL, W.HWND)
                post = api('user32', 'PostMessageW', W.BOOL, W.HWND, W.UINT, W.WPARAM, W.LPARAM)
                samples, previous = [], None
                for cycle in range(1, 21):
                    show(window, 6)
                    def minimized():
                        expect(process.poll() is None, 'Capture target exited while minimizing')
                        return iconic(window)
                    wait_until(minimized, 30, 'window minimize', .02)
                    request('Minimized', 'Timeout', 300)
                    show(window, 4)
                    wait_until(lambda: not iconic(window), 30, 'window restore', .02)
                    show(window, 0)
                    request('Restored')
                    expect(post(window, 0x100, 0x43, 1), 'Cannot send effect toggle key down')
                    time.sleep(.075)
                    expect(post(window, 0x101, 0x43, -1073741823), 'Cannot send effect toggle key up')
                    time.sleep(.075)
                    request('EffectCycle')
                    with Bitmap(self.project / f'{name}-EffectCycle.png') as bitmap:
                        pixel = bitmap.pixel(bitmap.width // 2, bitmap.height // 2)
                    expect(previous is None or sum(abs(a - b) for a, b in zip(pixel, previous)) >= 4, f'Screen Tint did not alternate on cycle {cycle}')
                    previous = pixel
                    samples.append(dict(cycle=cycle, effectRgb=pixel, **memory(process)))
                baseline = samples[0]['privateBytes']
                peak = max(item['privateBytes'] for item in samples)
                write_json(self.project / f'{name}-Memory.json', dict(processId=process.pid, windowCycles=len(samples), baselinePrivateBytes=baseline, peakPrivateBytes=peak,
                           finalPrivateBytes=samples[-1]['privateBytes'], allowedGrowthBytes=128 * 1024 ** 2, samples=samples))
                expect(peak - baseline <= 128 * 1024 ** 2, 'Private memory grew by more than 128 MiB during 20 window cycles')
                process.kill(); process.wait()
                request('Exited', 'Timeout', 150)
                request('Peer-After', target=peer)
            print(f'On-demand {name} passed; PID={process.pid}, SessionId={first["SessionId"]}')
        finally:
            for owned in (process, peer):
                stop_process(owned, executable)
