from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import stock_metadata


class StockMetadataTests(unittest.TestCase):
    def fixture(self, duplicate=False):
        b = bytearray(1024);b[:7] = b'\x7fELF\x01\x01\x01'
        struct.pack_into('<H', b, 18, 40);struct.pack_into('<I', b, 32, 768)
        struct.pack_into('<HH', b, 46, 40, 1)
        struct.pack_into('<10I', b, 768, 0, 1, 2, 0xe9100000, 128, 512, 0, 0, 4, 0)
        at = 384
        def string(s):
            nonlocal at
            p = at;raw = s.encode()+b'\0';b[p:p+len(raw)] = raw;at += len(raw)
            return 0xe9100000+p-128
        ident,year,title,maker,system = [string(s) for s in ('demo', '1995', 'Readable title', 'Fixture maker', 'Arcade')]
        struct.pack_into('<8I', b, 128, ident, 0, 0, year, title, 0, maker, system)
        if duplicate:struct.pack_into('<8I', b, 160, ident, 0, 0, year, string('Conflicting title'), 0, maker, system)
        return bytes(b)

    def test_reads_virtual_pointer_records_without_executing_module(self):
        self.assertEqual(stock_metadata.records(self.fixture()), {'demo': ('Readable title', '1995', 'Fixture maker')})

    def test_rejects_ambiguous_records_and_out_of_bounds_sections(self):
        with self.assertRaises(ValueError):stock_metadata.records(self.fixture(True))
        b = bytearray(self.fixture());struct.pack_into('<I', b, 768+20, 9999)
        with self.assertRaises(ValueError):stock_metadata.records(b)


if __name__ == '__main__':unittest.main()
