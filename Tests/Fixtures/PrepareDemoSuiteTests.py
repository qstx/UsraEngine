import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('prepare', Path(__file__).with_name('PrepareDemoSuite.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PreparationTests(unittest.TestCase):
    def test_isolation_repeated_preparation_and_catalog_errors(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = Path(__file__).resolve().parents[3] / 'Demo'
            target = root / 'TestProjects/DemoSuite/Debug'
            sample = root / 'TestProjects/Sample/Debug/Assets'
            sample.mkdir(parents=True)
            (sample / 'sentinel').write_text('keep')
            module.prepare(root, target, source)
            (target / 'Library').mkdir()
            (target / 'Library/stale').touch()
            (target / 'Assets/stale').touch()
            module.prepare(root, target, source)
            self.assertFalse((target / 'Assets/stale').exists())
            self.assertFalse((target / 'Library').exists())
            self.assertEqual((sample / 'sentinel').read_text(), 'keep')
            catalog_path = target / 'Assets/Tests/TestCatalog.ursaasset'
            baseline = json.loads(catalog_path.read_text(encoding='utf-8'))
            fields = baseline['payload']['Fields']
            for case in ('duplicate', 'missing', 'mismatch'):
                changed = json.loads(json.dumps(baseline))
                current = changed['payload']['Fields']
                if case == 'duplicate':
                    for key in ('ids', 'titles', 'descriptions', 'scenes'): current[key] += fields[key]
                elif case == 'missing': current['scenes'][0]['guid'] = '00000000-0000-0000-0000-000000000000'
                else: current['scenes'] = []
                catalog_path.write_text(json.dumps(changed), encoding='utf-8')
                with self.assertRaises(ValueError): module.validate_catalog(target)
            with self.assertRaises(ValueError): module.prepare(root, root / 'unsafe', source)


if __name__ == '__main__':
    unittest.main()
