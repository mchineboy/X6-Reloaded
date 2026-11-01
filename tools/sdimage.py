"""Create a sparse MBR/FAT32 SD image from regular local files only.

No disk discovery, device opens, mounting, formatting, or flash commands.
The first partition starts at 1 MiB; the SD-only boot data lives before it.
"""
from pathlib import Path, PurePosixPath
import math
import re
import stat
import struct

SECTOR = 512
PARTITION = 2048
SPC = 8
RESERVED = 32


def shortname(name):
    name = name.upper()
    if not re.fullmatch(r'[A-Z0-9_-]{1,8}(\.[A-Z0-9_-]{1,3})?', name):
        raise ValueError('Image requires short FAT names: ' + name)
    base, _, ext = name.partition('.')
    return base.ljust(8).encode() + ext.ljust(3).encode()


def entry(name, attr, first, size=0):
    result = bytearray(32)
    result[:11] = name
    result[11] = attr
    struct.pack_into('<H', result, 20, first >> 16)
    struct.pack_into('<H', result, 26, first & 0xffff)
    struct.pack_into('<I', result, 28, size)
    # Deterministic FAT date: 2026-11-01.
    struct.pack_into('<H', result, 24, ((2026 - 1980) << 9) | (11 << 5) | 1)
    return result


def build(output, files, stage, payload, size_mb=512):
    output = Path(output)
    if output.exists() and (output.is_symlink() or not stat.S_ISREG(output.stat().st_mode)):
        raise ValueError('Output must be a regular image file')
    files = dict(files)
    total_bytes = sum(Path(p).stat().st_size for p in files.values())
    size_mb = max(size_mb, math.ceil((total_bytes + 64 * 1024**2) / (128 * 1024**2)) * 128)
    if size_mb > 2048:
        raise ValueError('This X6 image recipe is limited to 2 GiB')
    total_sectors = size_mb * 2048
    volume = total_sectors - PARTITION
    fat_sectors = math.ceil((volume - RESERVED + 2 * SPC) * 4 / (SPC * SECTOR + 8))
    clusters = (volume - RESERVED - 2 * fat_sectors) // SPC
    if clusters < 65525 or (clusters + 2) * 4 > fat_sectors * SECTOR:
        raise ValueError('Invalid FAT32 geometry')
    dirs = {PurePosixPath('.'): []}
    for path, source in sorted(files.items()):
        path = PurePosixPath(path)
        if path.is_absolute() or '..' in path.parts or path == PurePosixPath('.'):
            raise ValueError('Unsafe image path')
        for parent in reversed(path.parents):
            dirs.setdefault(parent, [])
        source = Path(source)
        if not stat.S_ISREG(source.stat().st_mode):
            raise ValueError('Image inputs must be regular files')
        shortname(path.name)
    for d in sorted(dirs, key=str):
        if d != PurePosixPath('.'):
            shortname(d.name)
            dirs[d.parent].append((d, None))
    for name, source in sorted(files.items()):
        p = PurePosixPath(name)
        dirs[p.parent].append((p, Path(source)))
    for children in dirs.values():
        names = [shortname(p.name) for p, _ in children]
        if len(set(names)) != len(names):
            raise ValueError('Duplicate FAT name')
    fat = bytearray(fat_sectors * SECTOR)
    struct.pack_into('<II', fat, 0, 0x0ffffff8, 0xffffffff)
    allocated, next_cluster = {}, 2

    def allocate(path, nbytes):
        nonlocal next_cluster
        count = max(1, math.ceil(nbytes / (SPC * SECTOR)))
        first = next_cluster
        if first + count > clusters + 2:
            raise ValueError('Image is full')
        allocated[path] = (first, count)
        for cluster in range(first, first + count):
            struct.pack_into('<I', fat, cluster * 4,
                             cluster + 1 if cluster + 1 < first + count else 0x0fffffff)
        next_cluster += count

    root = PurePosixPath('.')
    for directory in sorted(dirs, key=lambda p: (len(p.parts), str(p))):
        allocate(directory, (len(dirs[directory]) + 3) * 32)
    for name, source in sorted(files.items()):
        allocate(PurePosixPath(name), Path(source).stat().st_size)
    if allocated[root][0] != 2:
        raise ValueError('Root allocation error')
    stage, payload = Path(stage).read_bytes(), Path(payload).read_bytes()
    if not stage or len(stage) % 512 or 16 * 512 + len(stage) > 64 * 512:
        raise ValueError('Stage overlaps payload')
    if not payload or len(payload) % 512 or 64 * 512 + len(payload) > PARTITION * 512:
        raise ValueError('Payload overlaps FAT partition')
    mbr = bytearray(512)
    mbr[446:462] = struct.pack('<B3sB3sII', 0, b'\xfe\xff\xff', 0x0c, b'\xfe\xff\xff', PARTITION, volume)
    mbr[510:] = b'\x55\xaa'
    boot = bytearray(512)
    boot[:11] = b'\xeb\x58\x90MSWIN4.1'
    struct.pack_into('<HBHBHHBHHHII', boot, 11, 512, SPC, RESERVED, 2, 0, 0, 0xf8, 0, 63, 255, PARTITION, volume)
    struct.pack_into('<IHHIHH', boot, 36, fat_sectors, 0, 0, 2, 1, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into('<I', boot, 67, 0x5836524c)
    boot[71:82] = b'X6 RELOADED'
    boot[82:90] = b'FAT32   '
    boot[510:] = b'\x55\xaa'
    info = bytearray(512)
    struct.pack_into('<I', info, 0, 0x41615252)
    struct.pack_into('<III', info, 484, 0x61417272, clusters - (next_cluster - 2), next_cluster)
    struct.pack_into('<I', info, 508, 0xaa550000)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('wb') as stream:
        stream.truncate(total_sectors * SECTOR)
        def put(sector, blob):
            stream.seek(sector * SECTOR)
            stream.write(blob)
        put(0, mbr)
        put(16, stage)
        put(64, payload)
        for sector in [0, 6]:
            put(PARTITION + sector, boot)
            put(PARTITION + sector + 1, info)
        for number in range(2):
            put(PARTITION + RESERVED + number * fat_sectors, fat)
        data_start = PARTITION + RESERVED + 2 * fat_sectors
        for directory, children in dirs.items():
            first = allocated[directory][0]
            data = bytearray()
            if directory == root:
                data += entry(b'X6 RELOADED', 8, 0)
            else:
                data += entry(b'.          ', 0x10, first)
                parent = 0 if directory.parent == root else allocated[directory.parent][0]
                data += entry(b'..         ', 0x10, parent)
            for path, source in sorted(children, key=lambda pair: str(pair[0])):
                data += entry(shortname(path.name), 0x10 if source is None else 0x20,
                              allocated[path][0], source.stat().st_size if source else 0)
            put(data_start + (first - 2) * SPC, data)
        for name, source in sorted(files.items()):
            stream.seek((data_start + (allocated[PurePosixPath(name)][0] - 2) * SPC) * SECTOR)
            with Path(source).open('rb') as incoming:
                while chunk := incoming.read(1024 * 1024):
                    stream.write(chunk)
    return {'bytes': total_sectors * SECTOR, 'partition_sector': PARTITION,
            'fat_sectors': fat_sectors, 'clusters': clusters, 'files': sorted(files)}
