"""Portable image handoff tests; synthetic files only, no media or firmware."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import sdimage
from verify_image import verify


class ImageVerificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='x6 image ')
        cls.root = Path(cls.temp.name)
        cls.image = cls.root / 'X6 Reloaded.img'
        cls.checksum = cls.root / 'X6 Reloaded.img.sha256'
        stage, payload = cls.root / 'stage', cls.root / 'payload'
        stage.write_bytes(b'S' * 512)
        payload.write_bytes(b'P' * 512)
        sdimage.build(cls.image, {'x6/boot.esh': payload}, stage, payload)
        cls.digest = cls.hash_image()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @classmethod
    def hash_image(cls):
        with cls.image.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()

    def setUp(self):
        self.checksum.write_text(self.digest + '  ' + self.image.name + '\n', encoding='utf-8')

    def test_native_cli_with_spaces_and_crlf(self):
        self.checksum.write_bytes((self.digest.upper() + ' *' + self.image.name + '\r\n').encode())
        result = subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] /
                                'tools/verify_image.py'), str(self.image)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('PASS:', result.stdout)
        self.assertIn('No card was accessed', result.stdout)

    def test_bad_checksum_and_wrong_image_name(self):
        for text in ['0' * 64 + '  ' + self.image.name + '\n',
                     self.digest + '  another.img\n', '',
                     self.digest + '  ' + self.image.name + '\nextra\n']:
            self.checksum.write_text(text)
            with self.assertRaises(ValueError):
                verify(self.image)

    def test_damage_rejected_even_with_recomputed_checksum(self):
        for offset, replacement in [(510, b'\0\0'), (450, b'\x07'), (454, b'\0' * 4),
                                    (462, b'\x01'), (2048 * 512 + 82, b'NTFS    ')]:
            with self.subTest(offset=offset):
                with self.image.open('r+b') as stream:
                    stream.seek(offset)
                    original = stream.read(len(replacement))
                    stream.seek(offset)
                    stream.write(replacement)
                try:
                    self.checksum.write_text(self.hash_image() + '  ' + self.image.name + '\n')
                    with self.assertRaises(ValueError):
                        verify(self.image)
                finally:
                    with self.image.open('r+b') as stream:
                        stream.seek(offset)
                        stream.write(original)

    def test_nonfiles_and_windows_device_paths_refused(self):
        for path in [self.root, self.root / 'missing.img', r'\\.\PhysicalDrive2', r'\\?\GLOBALROOT\Device\Harddisk0']:
            with self.assertRaises((ValueError, OSError)):
                verify(path)


if __name__ == '__main__':
    unittest.main()
