import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import title_index


class TitleIndexTests(unittest.TestCase):
    def dat(self, name, data):
        return ('game (\n name "' + name + '"\n rom ( name "' + name + '.unh" size ' + str(len(data))
                + f' crc {zlib.crc32(data):08x} sha1 {hashlib.sha1(data).hexdigest()} )\n)\n')

    def test_payload_match_ignores_ines_header_trainer_and_footer(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp);backup = root/'input';(backup/'game/nes').mkdir(parents=True)
            payload = b'payload data'.ljust(16384, b'\0')
            header = bytearray(b'NES\x1a' + bytes(12));header[4] = 1;header[6] = 4
            rom = bytes(header) + bytes(512) + payload + b'optional legacy title footer'
            path = backup/'game/nes/1114 ADVISL3.NES';path.write_bytes(rom)
            dat = root/'input.dat';dat.write_text(self.dat('Adventure Island 3 (USA)', payload))
            report = title_index.generate(backup, dat, root/'titles.tsv')
            self.assertEqual(report['titles_matched'], 1)
            self.assertIn('1114 ADVISL3.NES\tAdventure Island 3 (USA)', (root/'titles.tsv').read_text())
            self.assertEqual(path.read_bytes(), rom)
            self.assertIsNone(title_index.identify(payload[:-1] + b'x', title_index.database(dat)))
            with self.assertRaises(ValueError):title_index.generate(backup, dat, backup/'titles.tsv')

    def test_ambiguous_names_are_not_guessed_and_full_match_is_preferred(self):
        data = b'example';key = (len(data), zlib.crc32(data), hashlib.sha1(data).hexdigest())
        self.assertIsNone(title_index.identify(data, {key: {'Name one', 'Name two'}}))
        self.assertEqual(title_index.identify(data, {key: {'Exact title'}}), 'Exact title')


if __name__ == '__main__':unittest.main()
