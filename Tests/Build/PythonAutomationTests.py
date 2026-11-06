"""Regressions for migrated process, file, formatting and packaging boundaries."""
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Tools'))
from AutomationSupport import ENGINE, environment, inside, read_json, run, write_json
from Format import differs
sys.path.insert(0, str(ENGINE / 'Tests/Editor/Scripting'))
from VisualStudioSupport import Controller


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    instance = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(instance)
    return instance


packaging = module('TracyPackaging', ENGINE / 'Tools/TracyMcp/BuildRelease.py')
launcher = module('TracyLauncher', ENGINE / 'Tools/TracyMcp/Start-TracyMcp.py')


class AutomationTests(unittest.TestCase):
    def setUp(self):
        scratch = ENGINE / 'Build/PythonMigration'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='脚本 & automation ', dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()

    def test_argument_boundaries_and_expected_failure(self):
        value = 'spaces & 中文 " literal $()'
        result = run([sys.executable, '-c', 'import sys;print(sys.argv[1]);sys.exit(42)', value], expected=42)
        self.assertEqual(result.strip(), value)

    def test_environment_restored_after_failure(self):
        before = os.environ.get('URSA_MIGRATION_TEST')
        with self.assertRaisesRegex(RuntimeError, 'sentinel'):
            with environment(URSA_MIGRATION_TEST=self.root):
                self.assertEqual(os.environ['URSA_MIGRATION_TEST'], str(self.root))
                raise RuntimeError('sentinel')
        self.assertEqual(os.environ.get('URSA_MIGRATION_TEST'), before)

    def test_command_does_not_wait_for_descendants_output_handles(self):
        from WindowsAutomation import Process, stop_process
        command = 'import subprocess,sys; p=subprocess.Popen([sys.executable,"-c","import time;time.sleep(30)"],stdout=sys.stdout,stderr=sys.stderr); print(p.pid,flush=True)'
        started = time.monotonic()
        text = run([sys.executable, '-c', command], timeout=5)
        owned = Process(int(text.strip()))
        try:
            self.assertLess(time.monotonic() - started, 5, 'Command waited for descendant stdout EOF')
            self.assertIsNone(owned.poll())
        finally:
            stop_process(owned, sys.executable)
            owned.close()

    def test_unicode_json_atomic_replacement(self):
        path = self.root / '证据.json'
        write_json(path, {'Sequence': 1, 'Action': '开始'})
        write_json(path, {'Sequence': 2, 'Action': '停止'})
        self.assertEqual(read_json(path), {'Sequence': 2, 'Action': '停止'})
        self.assertFalse(path.with_suffix('.json.tmp').exists())

    def test_state_reader_allows_atomic_publication_while_open(self):
        from WindowsAutomation import shared_text_file
        path = self.root / 'State.json'
        write_json(path, {'Sequence': 1})
        with shared_text_file(path) as reader:
            write_json(path, {'Sequence': 2})
            self.assertIn('1', reader.read())
        self.assertEqual(read_json(path), {'Sequence': 2})

    def test_cleanup_boundary(self):
        for path in (self.root, self.root.parent, self.root.parent / (self.root.name + '-sibling')):
            with self.assertRaisesRegex(RuntimeError, 'outside'):
                inside(path, self.root)
        self.assertEqual(inside(self.root / 'nested/file', self.root), self.root / 'nested/file')

    def test_archive_validates_every_member_before_extraction(self):
        archive = self.root / 'bad.tar'
        with tarfile.open(archive, 'w') as stream:
            for name in ('python/valid.txt', '../escaped.txt'):
                item = tarfile.TarInfo(name); item.size = 1
                stream.addfile(item, io.BytesIO(b'x'))
        output = self.root / 'extract'
        output.mkdir()
        with self.assertRaisesRegex(RuntimeError, 'outside'):
            packaging.safe_extract(archive, output)
        self.assertEqual(list(output.iterdir()), [])

    def test_archive_rejects_links(self):
        archive = self.root / 'link.tar'
        with tarfile.open(archive, 'w') as stream:
            item = tarfile.TarInfo('python/link'); item.type = tarfile.SYMTYPE; item.linkname = '../../escape'
            stream.addfile(item)
        with self.assertRaisesRegex(RuntimeError, 'Unsupported archive member'):
            packaging.safe_extract(archive, self.root / 'extract')

    def test_publish_failure_restores_previous_release(self):
        staging, release, backup = [self.root / name for name in ('staging', 'release', 'backup')]
        staging.mkdir(); release.mkdir()
        (release / 'identity').write_text('previous', encoding='utf-8')
        rename = Path.rename
        def fail_stage(path, target):
            if path == staging:
                raise OSError('publication failure')
            return rename(path, target)
        with patch.object(Path, 'rename', fail_stage), self.assertRaisesRegex(OSError, 'publication failure'):
            packaging.publish(staging, release, backup)
        self.assertEqual((release / 'identity').read_text(encoding='utf-8-sig'), 'previous')
        self.assertFalse(backup.exists())
        self.assertTrue(staging.exists())

    def test_launch_rejects_missing_manifest(self):
        with self.assertRaisesRegex(RuntimeError, 'manifest is missing'):
            launcher.start(self.root, self.root / 'captures', 47380, check=True)

    def test_format_ignores_identical_replacements(self):
        source = self.root / 'Probe.cpp'
        source.write_bytes(b'a\n\n\tb')
        with patch('Format.subprocess.check_output', return_value=b'<replacements><replacement offset="1" length="3">&#10;&#10;&#9;</replacement></replacements>'):
            self.assertFalse(differs('formatter', source))
        with patch('Format.subprocess.check_output', return_value=b'<replacements><replacement offset="1" length="1"> </replacement></replacements>'):
            self.assertTrue(differs('formatter', source))

    def test_cli_help_without_powershell(self):
        commands = ['Tools/Format.py', 'Tools/AuditResourceIo.py', 'Tools/GenerateEditorIcon.py',
                    'Tools/CaptureRenderingBaseline.py', 'Tools/RunSrpPerformanceMatrix.py', 'Tools/ValidatePublicGltf.py',
                    'Tools/TracyMcp/BuildRelease.py', 'Tools/TracyMcp/Start-TracyMcp.py',
                    'Tests/Editor/Scripting/RunUnifiedIdeAcceptance.py', 'Tests/Editor/Scripting/RunManagedDebugAcceptance.py',
                    'Tests/Editor/Scripting/RunPrePlayRenderDebugAcceptance.py', 'Tests/Integration/AssetImportCliTests.py',
                    'Tests/Integration/ProjectPipelineSamplesTests.py', 'Tests/Editor/Workspace/AssetPreviewWorkspaceTests.py']
        env = dict(os.environ, PATH=str(Path(sys.executable).parent))
        for name in commands:
            with self.subTest(script=name):
                result = subprocess.run([sys.executable, str(ENGINE / name), '--help'], cwd=self.root, env=env,
                                        capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_controller_json_and_plain_action_responses(self):
        controller = Controller('controller.dll', 'fixture.slnx', self.root)
        with patch.object(controller, 'raw', return_value='Attached 123\n'):
            self.assertEqual(controller('attach', 123, 'engine'), 'Attached 123')
        with patch.object(controller, 'raw', return_value='{"Mode":2,"Line":11}\n'):
            self.assertEqual(controller('state'), {'Mode': 2, 'Line': 11})

    def test_controller_retries_busy_rpc(self):
        controller = Controller('controller.dll', 'fixture.slnx', self.root)
        busy = subprocess.CompletedProcess([], 1, '', 'RPC_E_CALL_REJECTED')
        ready = subprocess.CompletedProcess([], 0, '{"Mode":1}', '')
        with patch('VisualStudioSupport.subprocess.run', side_effect=[busy, ready]) as execute, patch('VisualStudioSupport.time.sleep'):
            self.assertEqual(controller('state'), {'Mode': 1})
            self.assertEqual(execute.call_count, 2)


if __name__ == '__main__':
    unittest.main()
