import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import stock


class StockImportTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.backup = self.root / 'backup'
        self.staging = self.root / 'staging'
        self.expected = {}
        for source, name in [('apps/mod/fba.mod', 'fba.mod'), ('game/mame/2020bb.fba', '2020bb.fba')]:
            content = ('test data for ' + source).encode()
            path = self.backup / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
            self.expected[source] = (name, len(content), hashlib.sha256(content).hexdigest())
        self.fixture = patch.object(stock, 'FILES', self.expected)
        self.fixture.start()
        self.addCleanup(self.fixture.stop)
        metadata = patch.object(stock.stock_metadata, 'extract', return_value='# ID\tTitle\n2020bb\tTest title\n')
        metadata.start()
        self.addCleanup(metadata.stop)

    def test_import_keeps_sources_and_saves_untouched(self):
        save = self.backup / 'game/mame/2020bb.fbasave'
        save.write_bytes(b'original save')
        report = stock.stage(self.backup, self.staging)
        self.assertFalse(report['hardware_tested'])
        self.assertFalse(report['saves_imported'])
        self.assertEqual(len(report['files']), 2)
        for row in report['files']:
            self.assertEqual((self.staging / row['path']).read_bytes(), Path(row['source']).read_bytes())
        self.assertEqual(save.read_bytes(), b'original save')
        self.assertFalse(any(self.staging.rglob('*.fbasave')))
        with self.assertRaises(ValueError):
            stock.stage(self.backup, self.staging)

    def test_rejects_unknown_inputs_before_creating_outputs(self):
        path = self.backup / 'game/mame/2020bb.fba'
        path.write_bytes(b'wrong')
        with self.assertRaises(ValueError):
            stock.stage(self.backup, self.staging)
        self.assertFalse(self.staging.exists())
        path.write_bytes(b'x' * self.expected['game/mame/2020bb.fba'][1])
        with self.assertRaises(ValueError):
            stock.stage(self.backup, self.staging)
        self.assertFalse(self.staging.exists())

    def test_rejects_symlinks_and_input_output_overlap(self):
        source = self.backup / 'apps/mod/fba.mod'
        actual = self.root / 'actual'
        source.rename(actual)
        source.symlink_to(actual)
        with self.assertRaises(ValueError):
            stock.inspect(self.backup)
        source.unlink()
        actual.rename(source)
        for output in [self.backup, self.backup / 'nested', self.root]:
            with self.assertRaises(ValueError):
                stock.stage(self.backup, output)
        outside = self.root / 'outside'
        outside.mkdir()
        (self.root / 'staging').mkdir()
        (self.staging / 'x6').symlink_to(outside, target_is_directory=True)
        with self.assertRaises(ValueError):
            stock.stage(self.backup, self.staging)
        self.assertEqual(list(outside.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
