"""Read title records from the user's exact original FBA binary, without executing it.

Generated title data stays in the private image, not in the source repository.
Finding a record establishes identity; it is not a device compatibility test.
"""
import hashlib
from pathlib import Path
import struct

BASELINE = '98d5b12acbef39a0b3b9ec4c367c7cebd8a975b4391472fa763090d4aca6b759'


def records(blob):
    if len(blob) < 52 or blob[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Expected original ARM ELF32 module')
    if struct.unpack_from('<H', blob, 18)[0] != 40:
        raise ValueError('Expected ARM module')
    shoff = struct.unpack_from('<I', blob, 32)[0]
    size, count = struct.unpack_from('<HH', blob, 46)
    if size != 40 or shoff + size * count > len(blob):
        raise ValueError('Invalid ELF section table')
    sections = []
    for i in range(count):
        s = struct.unpack_from('<10I', blob, shoff + size * i)
        if s[1] != 8 and s[2] & 2:
            if s[4] + s[5] > len(blob):
                raise ValueError('Invalid ELF section bounds')
            sections.append((s[3], s[4], s[5]))

    def string(address):
        for base, offset, length in sections:
            if base <= address < base + length:
                start = offset + address - base
                end = blob.find(b'\0', start, min(start + 256, offset + length))
                raw = blob[start:end]
                if end >= start and raw and all(32 <= c < 127 for c in raw):
                    return raw.decode('ascii')
        return None

    result = {}
    for _, offset, length in sections:
        for at in range((offset + 3) & ~3, offset + length - 31, 4):
            words = struct.unpack_from('<8I', blob, at)
            ident, year, title, maker, system = (string(words[i]) for i in (0, 3, 4, 6, 7))
            if not (ident and len(ident) < 16 and all(c.isascii() and (c.isalnum() or c == '_') for c in ident)
                    and year and len(year) == 4 and all(c.isdigit() or c == '?' for c in year)
                    and title and maker and system):
                continue
            row = (title, year, maker)
            if ident in result and result[ident] != row:
                raise ValueError('Ambiguous stock metadata ID: ' + ident)
            result[ident] = row
    if not result:
        raise ValueError('No stock metadata records found')
    return result


def extract(module):
    blob = Path(module).read_bytes()
    if hashlib.sha256(blob).hexdigest() != BASELINE:
        raise ValueError('Unsupported original FBA metadata baseline')
    result = records(blob)
    return '# ID\tTitle\tYear\tMaker; generated from your original FBA module\n' + ''.join(
        '\t'.join((ident, *result[ident])) + '\n' for ident in sorted(result))
