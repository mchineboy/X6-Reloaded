"""Backup failure handling and FEL safety checks; no USB/card access."""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import backup
import write_sd


class BackupTests(unittest.TestCase):
    def setUp(self):
        quiet = patch('sys.stdout', new_callable=io.StringIO)
        quiet.start()
        self.addCleanup(quiet.stop)

    def identity(self, version='AWUSBFEX ID=0x00166300(F1C100S)', capacity=8388608):
        return patch.object(backup.subprocess, 'check_output', side_effect=[
            version, f"Found spi nor flash 'XT25F64F' with {capacity} bytes\n"])

    def test_wrong_chip_or_capacity_stops_before_reading(self):
        for version, size in [('ID=0x00166500', 8388608), ('ID=0x00166300', 16777216),
                              ('ERROR: No FEL device found!', 8388608)]:
            with self.subTest(version=version, size=size), self.identity(version, size), \
                    patch.object(backup.subprocess, 'run') as run:
                with self.assertRaises(ValueError):
                    backup.backup(xfel='test-xfel')
                run.assert_not_called()

    def fake_read(self, contents):
        calls = []

        def run(command, **kwargs):
            self.assertEqual(command[:5], ['test-xfel', 'spinor', 'read', '0', '0x800000'])
            path = Path(command[5])
            self.assertFalse(path.exists())
            path.write_bytes(contents[len(calls)])
            calls.append(command)
            return subprocess.CompletedProcess(command, 0, stdout='read complete\n')
        return calls, run

    def test_two_reads_manifest_and_existing_backups_preserved(self):
        payload = b'X6 data\0' * (backup.FLASH_BYTES // 8)
        with tempfile.TemporaryDirectory() as tmp, self.identity():
            root = Path(tmp)
            old = root / 'local/backups/existing.bin'
            old.parent.mkdir(parents=True)
            old.write_bytes(b'keep original')
            calls, run = self.fake_read([payload, payload])
            with patch.object(backup.subprocess, 'run', side_effect=run):
                folder = backup.backup(root, 'test-xfel')
            report = json.loads((folder / 'verification.json').read_text())
            self.assertEqual(len(calls), 2)
            self.assertTrue(report['reads_identical'])
            self.assertEqual(report['sha256'], hashlib.sha256(payload).hexdigest())
            self.assertFalse(report['flash_program_or_erase_commands_used'])
            self.assertFalse(report['restore_test_performed'])
            self.assertEqual(old.read_bytes(), b'keep original')

    def test_short_mismatched_and_empty_reads_are_not_verified(self):
        normal = b'x' * backup.FLASH_BYTES
        for contents in [[b'short'], [normal, b'y' * backup.FLASH_BYTES],
                         [bytes(backup.FLASH_BYTES)] * 2, [b'\xff' * backup.FLASH_BYTES] * 2]:
            with self.subTest(size=len(contents[0])), tempfile.TemporaryDirectory() as tmp, self.identity():
                root = Path(tmp)
                calls, run = self.fake_read(contents)
                with patch.object(backup.subprocess, 'run', side_effect=run):
                    with self.assertRaises(ValueError):
                        backup.backup(root, 'test-xfel')
                folder, = (root / 'local/backups').iterdir()
                report = json.loads((folder / 'verification.json').read_text())
                self.assertFalse(report['reads_identical'])
                self.assertIn('error', report)
                self.assertTrue((folder / 'internal-flash-read1.bin').exists())

    def test_failed_xfel_records_failure_even_with_full_file(self):
        with tempfile.TemporaryDirectory() as tmp, self.identity():
            calls, read = self.fake_read([b'x' * backup.FLASH_BYTES])

            def failed(command, **kwargs):
                read(command, **kwargs)
                return subprocess.CompletedProcess(command, 1, stdout='USB failed\n')

            with patch.object(backup.subprocess, 'run', side_effect=failed):
                with self.assertRaises(subprocess.CalledProcessError):
                    backup.backup(Path(tmp), 'test-xfel')
            record, = (Path(tmp) / 'local/backups').glob('*/verification.json')
            self.assertFalse(json.loads(record.read_text())['reads_identical'])


class FELImageTests(unittest.TestCase):
    def test_wrong_helper_refused_even_if_image_envelope_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / 'FEL.img'
            image.write_bytes(bytes(16384))
            with patch.object(backup, 'verify', return_value={'image_sha256': 'test'}):
                with self.assertRaisesRegex(ValueError, 'helper differs'):
                    backup.verify_fel(image)

    def test_fel_writer_rejects_bad_helper_before_any_card_inspection(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'dist').mkdir()
            image = root / 'dist/X6-FEL.img'
            image.write_bytes(bytes(16384))
            with patch.object(write_sd, 'ROOT', root), \
                    patch.object(write_sd.sys, 'argv', ['write_sd.py', '--fel']), \
                    patch.object(write_sd.sys, 'platform', 'darwin'), \
                    patch.dict(write_sd.os.environ, {'DISK': '/dev/disk9'}), \
                    patch.object(backup, 'verify', return_value={'image_sha256': 'test'}), \
                    patch.object(write_sd, 'inspect') as inspect:
                with self.assertRaisesRegex(ValueError, 'helper differs'):
                    write_sd.main()
                inspect.assert_not_called()


if __name__ == '__main__':
    unittest.main()
