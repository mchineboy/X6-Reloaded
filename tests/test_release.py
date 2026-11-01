"""Host-side safety, ROM conversion, and independently decoded FAT32 tests."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import audit_mame_roms as roms
import firmware
import sdimage
from write_sd import validate


class ROMTests(unittest.TestCase):
    def test_renamed_rom_is_verified_by_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            payload = bytes(range(256))
            source = folder / 'driver.c'
            source.write_text('ROM_START(test)\nROM_REGION(256, 0, 0)\n'
                f'ROM_LOAD("expected.bin", 0, 256, 0x{zlib.crc32(payload):08x})\nROM_END\n')
            archive = folder / 'source.zip'
            with zipfile.ZipFile(archive, 'w') as out:
                out.writestr('../different-name.bin', payload)
            before = archive.read_bytes()
            output = folder / 'rebuilt.zip'
            self.assertTrue(roms.audit(archive, source, 'test', output)['output_verified'])
            self.assertEqual(archive.read_bytes(), before)
            with zipfile.ZipFile(output) as out:
                self.assertEqual(out.namelist(), ['expected.bin'])
                self.assertEqual(out.read('expected.bin'), payload)

    def test_wrong_bytes_and_unsupported_macros_fail(self):
        with tempfile.TemporaryDirectory() as folder:
            archive = Path(folder) / 'bad.zip'
            with zipfile.ZipFile(archive, 'w') as out:
                out.writestr('expected.bin', b'wrong')
            with zipfile.ZipFile(archive) as src:
                self.assertEqual(roms.matching_bytes(src, 'test', {'name': 'expected.bin', 'bytes': 5, 'crc32': 1}), (None, {}))
        with self.assertRaises(ValueError):
            roms.requirements('ROM_START(test) ROM_LOAD_UNKNOWN("a",0,1,1) ROM_END', 'test')

    def test_continue_reload_size(self):
        spec = roms.requirements('ROM_START(test) ROM_LOAD("a",0,4,1) ROM_CONTINUE(8,4) ROM_RELOAD(16,8) ROM_END', 'test')
        self.assertEqual(spec[0]['bytes'], 8)


class FirmwareTests(unittest.TestCase):
    def test_unknown_firmware_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'wrong.bin'
            source.write_bytes(b'not firmware')
            with self.assertRaisesRegex(ValueError, '8 MiB'):
                firmware.extract_stock(source)
            self.assertEqual(source.read_bytes(), b'not firmware')

    def test_checksum_field_and_overflow(self):
        source = bytearray(512)
        struct.pack_into('<I', source, 20, 0xffffffff)
        result = firmware.egon_checksum(bytes(source))
        stamp = bytearray(result)
        struct.pack_into('<I', stamp, 12, 0x5f0a6c39)
        self.assertEqual(firmware.word(result, 12), firmware.sum32(bytes(stamp)))
        self.assertEqual(source[12:16], b'\0' * 4)


class DiskSafetyTests(unittest.TestCase):
    def info(self):
        return {'WholeDisk': True, 'RemovableMedia': True, 'WritableMedia': True,
                'VirtualOrPhysical': 'Physical', 'BusProtocol': 'USB', 'Internal': False,
                'TotalSize': 16 * 1024**3, 'DeviceTreePath': 'test/usb/card', 'DeviceIdentifier': 'disk9'}

    def test_only_explicit_removable_whole_disk(self):
        validate('/dev/disk9', self.info(), 512 * 1024**2)
        for disk in ['/dev/disk0', '/dev/disk9s1', '/tmp/not-a-disk', '']:
            with self.assertRaises(ValueError):
                validate(disk, self.info(), 512)
        for key, value in [('RemovableMedia', False), ('WholeDisk', False), ('WritableMedia', False),
                           ('VirtualOrPhysical', 'Virtual'), ('BusProtocol', 'PCI-Express'),
                           ('Internal', True), ('DeviceTreePath', None), ('TotalSize', 100)]:
            info = self.info()
            info[key] = value
            with self.assertRaises(ValueError):
                validate('/dev/disk9', info, 512 * 1024**2)


class ImageTests(unittest.TestCase):
    def test_fat_chains_directories_and_content(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root / 'payload'
            content = b'content' * 3000
            source.write_bytes(content)
            stage, payload = root / 'stage.bin', root / 'boot.bin'
            stage.write_bytes(b'S' * 512)
            payload.write_bytes(b'P' * 1024)
            image = root / 'test.img'
            sdimage.build(image, {'game/cps/dkong.zip': source, 'x6/boot.esh': source}, stage, payload)
            with image.open('rb') as stream:
                def read(offset, count):
                    stream.seek(offset)
                    return stream.read(count)
                mbr = read(0, 512)
                self.assertEqual(mbr[510:], b'\x55\xaa')
                part, sectors = struct.unpack_from('<II', mbr, 454)
                self.assertEqual(part, 2048)
                self.assertEqual((part + sectors) * 512, image.stat().st_size)
                self.assertEqual(read(16 * 512, 512), stage.read_bytes())
                self.assertEqual(read(64 * 512, 1024), payload.read_bytes())
                bpb = read(part * 512, 512)
                self.assertEqual(bpb[82:90], b'FAT32   ')
                self.assertEqual(read((part + 6) * 512, 512), bpb)
                self.assertEqual(struct.unpack_from('<I', bpb, 28)[0], part)
                spc, reserved = bpb[13], struct.unpack_from('<H', bpb, 14)[0]
                fatsize = struct.unpack_from('<I', bpb, 36)[0]
                fat = read((part + reserved) * 512, fatsize * 512)
                self.assertEqual(read((part + reserved + fatsize) * 512, len(fat)), fat)
                start = part + reserved + 2 * fatsize
                def chain(cluster):
                    data, visited = bytearray(), set()
                    while cluster < 0x0ffffff8:
                        self.assertNotIn(cluster, visited)
                        visited.add(cluster)
                        data += read((start + (cluster - 2) * spc) * 512, spc * 512)
                        cluster = struct.unpack_from('<I', fat, cluster * 4)[0] & 0x0fffffff
                    return data
                def directory(cluster):
                    result = {}
                    data = chain(cluster)
                    for offset in range(0, len(data), 32):
                        entry = data[offset:offset + 32]
                        if entry[0] == 0:
                            break
                        name = entry[:8].decode().strip()
                        ext = entry[8:11].decode().strip()
                        name += '.' + ext if ext else ''
                        first = (struct.unpack_from('<H', entry, 20)[0] << 16) | struct.unpack_from('<H', entry, 26)[0]
                        result[name] = (first, struct.unpack_from('<I', entry, 28)[0])
                    return result
                top = directory(2)
                game = directory(top['GAME'][0])
                cps = directory(game['CPS'][0])
                first, size = cps['DKONG.ZIP']
                self.assertEqual(chain(first)[:size], content)
                x6 = directory(top['X6'][0])
                first, size = x6['BOOT.ESH']
                self.assertEqual(chain(first)[:size], content)

    def test_no_traversal_or_device_outputs(self):
        with self.assertRaises(ValueError):
            sdimage.shortname('../file')
        with self.assertRaises(ValueError):
            sdimage.shortname('too-long-name.zip')

if __name__ == '__main__':
    unittest.main()
