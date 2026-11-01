#!/usr/bin/env python3
"""Read an ARM ELF32 module and report sections and ePDK MAGIC metadata.

This inspects an ordinary local file. It neither executes the file nor accesses
the handheld. The seven interface labels follow conventional Melis naming;
their presence does not establish the separate arcade-emulator interface.
"""
import argparse
import hashlib
import json
from pathlib import Path
import stat
import struct


def inspect(path):
    if not stat.S_ISREG(path.stat().st_mode):
        raise ValueError('Only regular files are accepted.')
    data = path.read_bytes()
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Expected a little-endian ELF32 file.')
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    if header[2] != 40 or header[8] != 52:
        raise ValueError('Expected ARM ELF with the standard ELF32 header.')
    shoff, shsize, shnum, shstr = header[6], header[11], header[12], header[13]
    if shsize != 40 or not shnum or shstr >= shnum:
        raise ValueError('Unsupported or missing section table.')

    def read(offset, size):
        if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
            raise ValueError('Section/table lies outside the file.')
        return data[offset:offset + size]

    read(shoff, shsize * shnum)
    sections = [struct.unpack_from('<10I', data, shoff + i * shsize) for i in range(shnum)]
    name_section = sections[shstr]
    if name_section[1] != 3:
        raise ValueError('Section-name table is not a string table.')
    names = read(name_section[4], name_section[5])
    output = []
    magic = None
    for item in sections:
        if item[0] >= len(names):
            raise ValueError('Invalid section-name offset.')
        end = names.find(b'\0', item[0])
        if end < 0:
            raise ValueError('Unterminated section name.')
        name = names[item[0]:end].decode('ascii', errors='replace')
        payload = b'' if item[1] == 8 else read(item[4], item[5])
        output.append({'name': name, 'type': item[1], 'address': hex(item[3]),
                       'size': item[5], 'offset': item[4], 'flags': hex(item[2])})
        if name == 'MAGIC':
            if magic is not None or len(payload) != 52 or payload[:8] != b'ePDK.mod':
                raise ValueError('Unsupported or duplicate ePDK MAGIC metadata.')
            words = struct.unpack('<13I', payload)
            labels = ['MInit', 'MExit', 'MOpen', 'MClose', 'MRead', 'MWrite', 'MIoctrl']
            magic = {'raw_hex': payload.hex(), 'header_words_after_signature':
                     [hex(word) for word in words[2:6]],
                     'conventional_entry_names': dict(zip(labels, [hex(word) for word in words[6:]])),
                     'emulator_interface_verified': False}
    if magic is None:
        raise ValueError('No supported ePDK.mod MAGIC section found.')
    return {'file': str(path), 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
            'elf_entry': hex(header[4]), 'sections': output, 'magic': magic}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('module', type=Path)
    args = parser.parse_args()
    try:
        result = inspect(args.module)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f'Inspection failed: {error}\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
