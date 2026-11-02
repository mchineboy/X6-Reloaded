from pathlib import Path, PurePosixPath
import struct
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import sdimage


class FatNamesTests(unittest.TestCase):
    def test_long_names_unicode_and_alias_collisions_round_trip(self):
        names = ['Double Dragon (Neo-Geo).fba', 'Double Dragon (Japan).fba', '中文 Game.nes', 'Straße.nes',
                 'Very Long Name.nes', 'VERYL~1.NES', 'abcdefghijklm', 'ordinary.zip']
        children = [(PurePosixPath(n), None) for n in names]
        result = sdimage.directory_names(children)
        self.assertEqual(len({v[0] for v in result.values()}), len(names))
        for path, (dos, long) in result.items():
            checksum = 0
            for byte in dos:checksum = (((checksum & 1)<<7) + (checksum>>1) + byte) & 255
            if long:
                self.assertTrue(long[0][0] & 0x40)
                parts = []
                for entry in reversed(long):
                    self.assertEqual(entry[11], 0x0f);self.assertEqual(entry[13], checksum)
                    self.assertEqual(entry[26:28], b'\0\0')
                    parts.append(entry[1:11]+entry[14:26]+entry[28:32])
                decoded = b''.join(parts).decode('utf-16-le').split('\0')[0].rstrip('\uffff')
                self.assertEqual(decoded, path.name)

    def test_case_duplicates_and_unsafe_names_are_rejected(self):
        for names in [('same.nes', 'SAME.NES'), ('invalid?.nes',), ('trailing.',)]:
            with self.assertRaises(ValueError):sdimage.directory_names([(PurePosixPath(n), None) for n in names])

    def test_sparse_image_larger_than_two_gib_has_consistent_fat32_geometry(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp);stage=root/'stage';payload=root/'payload'
            stage.write_bytes(bytes(512));payload.write_bytes(bytes(512))
            image=root/'large.img';g=sdimage.build(image, {}, stage, payload, size_mb=4096)
            self.assertEqual(image.stat().st_size, 4096*1024**2)
            with image.open('rb') as f:
                f.seek(sdimage.PARTITION*512);boot=f.read(512)
            self.assertEqual(struct.unpack_from('<I',boot,32)[0]+sdimage.PARTITION,g['bytes']//512)
            self.assertEqual(struct.unpack_from('<I',boot,36)[0],g['fat_sectors'])


if __name__ == '__main__':unittest.main()
