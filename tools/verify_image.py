#!/usr/bin/env python3
"""Verify a local X6 SD image before opening it in a Windows/macOS/Linux imager.

Reads regular files only. This checks the checksum and MBR/FAT32 envelope, not
firmware compatibility, every FAT chain, device readback, or successful X6 boot.
"""
import argparse
import hashlib
from pathlib import Path
import re
import stat
import struct

ROOT = Path(__file__).resolve().parents[1]


def regular_file(path):
    path = Path(path)
    # Reject Windows device namespaces even when this function runs on Unix.
    if str(path).startswith(('\\\\.\\', '\\\\?\\')):
        raise ValueError('Expected a regular local file, never a device path')
    if path.is_symlink() or not stat.S_ISREG(path.stat().st_mode):
        raise ValueError('Expected a regular local file: ' + str(path))
    return path


def verify(image, checksum=None):
    image = regular_file(image)
    checksum = regular_file(checksum or image.with_name(image.name + '.sha256'))
    lines = checksum.read_text(encoding='utf-8-sig').splitlines()
    if len(lines) != 1:
        raise ValueError('Expected one SHA-256 checksum line')
    match = re.fullmatch(r'([0-9a-fA-F]{64}) [ *](.+)', lines[0])
    if not match or match[2] != image.name:
        raise ValueError('Checksum must name this image exactly')
    expected = match[1].lower()
    with image.open('rb') as stream:
        actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if actual != expected:
            raise ValueError('Image checksum differs; rebuild or recopy before writing')
        stream.seek(0, 2)
        size = stream.tell()
        stream.seek(0)
        mbr = stream.read(512)
        if len(mbr) != 512 or mbr[510:] != b'\x55\xaa' or mbr[450] != 0x0c:
            raise ValueError('Expected an MBR with a FAT32 LBA partition')
        start, sectors = struct.unpack_from('<II', mbr, 454)
        if start != 2048 or not sectors or (start + sectors) * 512 != size or any(mbr[462:510]):
            raise ValueError('Expected one FAT32 partition at 1 MiB filling the image')
        stream.seek(start * 512)
        boot = stream.read(512)
        if (boot[510:] != b'\x55\xaa' or boot[82:90] != b'FAT32   '
                or struct.unpack_from('<H', boot, 11)[0] != 512
                or struct.unpack_from('<I', boot, 28)[0] != start
                or struct.unpack_from('<I', boot, 32)[0] != sectors):
            raise ValueError('FAT32 boot sector differs from the image partition')
    return {'image_sha256': actual, 'bytes': size, 'partition_sector': start,
            'filesystem': 'FAT32'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', nargs='?', type=Path, default=ROOT / 'dist/X6-Reloaded.img')
    parser.add_argument('--checksum', type=Path, help='default: IMAGE.sha256')
    args = parser.parse_args()
    try:
        result = verify(args.image, args.checksum)
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + '\n')
    print(f"PASS: {args.image.name}: SHA-256 matches; MBR/FAT32 at 1 MiB; {result['bytes']} bytes")
    print('No card was accessed. Write the entire .img with an imager and complete its verification.')


if __name__ == '__main__':
    main()
