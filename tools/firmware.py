"""Build SD-only boot data from a user's exact compatible stock dump.

Reads regular files. Never opens a device, downloads firmware, or writes inputs.
The output contains vendor-derived bytes and MUST stay private.
"""
import hashlib
import lzma
from pathlib import Path
import stat
import struct
import subprocess

BASELINE = {
    'boot0': '22ce893974c5caeb61e5740804a886993edf78e5a6f3c8a995d75829993faf83',
    'boot1': '8e68e238165be3f56aef869a441105e1cca625d246d4b9521d1a529f497f730b',
    'ramdisk': '922501a322a6191d7bfc1f80975d5e516fac833a53133bff3f2074ef61d9bdc9',
}
PATCHES = [(0x80408cd0, 0xe1a00004, 0xe51ff004),
           (0x80408cd4, 0xeb0021a7, 0x81000000),
           (0x804037f8, 1, 0), (0x80403858, 1, 0)]


def word(blob, off):
    return struct.unpack_from('<I', blob, off)[0]


def sum32(blob):
    padded = blob + b'\0' * (-len(blob) % 4)
    return sum(x[0] for x in struct.iter_unpack('<I', padded)) & 0xffffffff


def entries(blob):
    if blob[:5] != b'MINFS':
        raise ValueError('Expected MinFS image')
    offset, count = word(blob, 8), word(blob, 16)
    for _ in range(count):
        data, size, unpacked, length, attr, namesize, extent = struct.unpack_from('<IIIHHHH', blob, offset)
        if length < 20 + namesize or offset + length > len(blob):
            raise ValueError('Bad MinFS directory')
        name = blob[offset + 20:offset + 20 + namesize].decode('utf-8', errors='replace')
        yield name, data, size, offset + 20 + ((namesize + 3) & ~3), extent, attr
        offset += length


def extract_stock(path):
    if not stat.S_ISREG(path.stat().st_mode):
        raise ValueError('Firmware must be a regular backup file, never a device')
    dump = path.read_bytes()
    if len(dump) != 8 * 1024 * 1024:
        raise ValueError('Expected an 8 MiB V1.241017 internal-flash backup')
    root = dump[0x24400:]
    matches = [e for e in entries(root) if e[0] == 'ramdisk.iso']
    if len(matches) != 1 or matches[0][5] & 4:
        raise ValueError('Stock ramdisk not found uncompressed')
    _, off, size, _, _, _ = matches[0]
    parts = {'boot0': dump[:0x4200], 'boot1': dump[0x6000:0x17c00], 'ramdisk': root[off:off + size]}
    for name, blob in parts.items():
        if hashlib.sha256(blob).hexdigest() != BASELINE[name]:
            raise ValueError('Unsupported firmware: ' + name + ' differs from the verified V1.241017 baseline')
    for address, expected, _ in PATCHES:
        if word(parts['boot1'], address - 0x80400000) != expected:
            raise ValueError('Boot1 patch preflight failed')
    return parts


def patched_ramdisk(original):
    result = bytearray(original)
    shell = next(e for e in entries(original) if e[0] == 'shell.zgj')
    _, file_off, _, ext, extlen, attr = shell
    if extlen < 32 or attr & 6 != 6:
        raise ValueError('Unexpected shell module layout')
    off, stored, unpacked, _, vaddr, _, _, flags = struct.unpack_from('<8I', original, ext)
    if not flags & 2 or vaddr != 0:
        raise ValueError('Unexpected shell code section')
    compressed = original[file_off + off:file_off + off + stored]
    props = compressed[:5]
    p = props[0]
    filt = {'id': lzma.FILTER_LZMA1, 'dict_size': word(props, 1),
            'lc': p % 9, 'lp': p // 9 % 5, 'pb': p // 45,
            'mode': lzma.MODE_NORMAL, 'mf': lzma.MF_BT3, 'nice_len': 64}
    raw = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=[filt]).decompress(compressed[5:], max_length=unpacked)
    if len(raw) != unpacked or word(raw, 0x12774) != 0x7a74:
        raise ValueError('Shell handler preflight failed')
    code = bytearray(raw)
    struct.pack_into('<I', code, 0x12774, 0x808)
    encoded = props + lzma.compress(bytes(code), format=lzma.FORMAT_RAW, filters=[filt])
    if len(encoded) > stored:
        raise ValueError('Patched shell does not fit its original slot')
    decoded = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=[filt]).decompress(encoded[5:], max_length=unpacked)
    if decoded != bytes(code):
        raise ValueError('Shell compression round-trip failed')
    result[file_off + off:file_off + off + stored] = encoded + b'\0' * (stored - len(encoded))
    struct.pack_into('<I', result, ext + 4, len(encoded))
    startup = next(e for e in entries(original) if e[0] == 'startup.esh')
    _, at, size, _, _, attr = startup
    if attr & 4:
        raise ValueError('Unexpected compressed startup script')
    script = original[at:at + size]
    begin, end = script.index(b'# Install modules\r\n'), script.index(b'# startup script work end')
    body = b'poweroff\r\nsrc f:\\\\x6\\\\boot.esh\r\nstartx\r\n'
    # Preserve the entry length and all other stock startup content.
    remaining = end - begin - len(body)
    if remaining < 3:
        raise ValueError('Startup script slot too short')
    body += b'#' + b' ' * (remaining - 3) + b'\r\n'
    result[at + begin:at + end] = body
    if len(result) != len(original):
        raise ValueError('Ramdisk size changed')
    return bytes(result)


def egon_checksum(blob):
    result = bytearray(blob)
    struct.pack_into('<I', result, 12, 0x5f0a6c39)
    struct.pack_into('<I', result, 12, sum32(bytes(result)))
    return bytes(result)


def build(root, firmware, toolchain):
    stock = extract_stock(firmware)
    out = root / 'build/boot'
    out.mkdir(parents=True, exist_ok=True)
    tools = toolchain / 'bin'
    gcc, ld, objcopy = [str(tools / ('arm-none-eabi-' + t)) for t in ['gcc', 'ld', 'objcopy']]
    flags = ['-Os', '-marm', '-march=armv5te', '-mfloat-abi=soft', '-ffreestanding',
             '-fno-builtin', '-nostdlib', '-fno-pic', '-ffunction-sections', '-fdata-sections',
             '-I' + str(root / 'include')]
    libgcc = subprocess.check_output([gcc, '-print-libgcc-file-name'], text=True).strip()
    for name in ['sd.c', 'hook.c', 'stub-ramdisk.S', 'x6menu.c', 'session.c']:
        subprocess.run([gcc, *flags, '-c', str(root / 'boot' / name), '-o', str(out / (name + '.o'))], check=True)
    ui = []
    for name in ['gfx.c', 'font_data.c', 'splash.c', 'menu.c', 'library.c']:
        obj = out / (name + '.o')
        subprocess.run([gcc, *flags, '-c', str(root / 'src/ui' / name), '-o', str(obj)], check=True)
        ui.append(str(obj))
    timing = out / 'timing.o'
    subprocess.run([gcc, *flags, '-c', str(root / 'src/arcade/timing.c'), '-o', str(timing)], check=True)
    discovery = []
    for source in ['src/runtime/directory.c', 'src/arcade/stock_catalog.c', 'src/arcade/titles.c', 'src/arcade/libraries.c']:
        obj = out / (Path(source).stem + '.o')
        subprocess.run([gcc, *flags, '-c', str(root / source), '-o', str(obj)], check=True)
        discovery.append(str(obj))
    subprocess.run([ld, '--gc-sections', '-T', str(root / 'boot/exf.ld'),
                    str(out / 'x6menu.c.o'), str(out / 'session.c.o'), str(timing),
                    *ui, *discovery, libgcc, '-o', str(out / 'x6menu.axf')], check=True)
    subprocess.run([ld, '-T', str(root / 'boot/hook-sram.ld'), str(out / 'hook.c.o'),
                    str(out / 'sd.c.o'), libgcc, '-o', str(out / 'hook.elf')], check=True)
    subprocess.run([objcopy, '-O', 'binary', str(out / 'hook.elf'), str(out / 'hook.bin')], check=True)
    subprocess.run([objcopy, '-O', 'binary', '-j', '.text', str(out / 'stub-ramdisk.S.o'), str(out / 'stub.bin')], check=True)
    boot0 = bytearray(stock['boot0'])
    for at in [0x2b4c, 0x2964]:
        # The full boot0 hash was checked before changing branch displacements.
        target = 0x4200 if at == 0x2b4c else 0x2970
        branch = 0xea000000 | (((target - at - 8) // 4) & 0xffffff)
        struct.pack_into('<I', boot0, at, branch)
    stage = boot0 + (out / 'hook.bin').read_bytes()
    stage += b'\0' * (-len(stage) % 512)
    if len(stage) > (64 - 16) * 512:
        raise ValueError('Boot hook overlaps payload')
    struct.pack_into('<I', stage, 16, len(stage))
    (out / 'stage.bin').write_bytes(egon_checksum(bytes(stage)))
    items, data = [], bytearray()
    for blob, address in [((out / 'stub.bin').read_bytes(), 0x81000000),
                          (patched_ramdisk(stock['ramdisk']), 0x81100000)]:
        items.append((1 + len(data) // 512, len(blob), address, sum32(blob)))
        data += blob + b'\0' * (-len(blob) % 512)
    header = bytearray(struct.pack('<5I', 0x4c503658, 1, len(items), len(PATCHES), 0))
    for item in items + [(0, 0, 0, 0)] * (8 - len(items)):
        header += struct.pack('<4I', *item)
    for patch in PATCHES + [(0, 0, 0)] * (16 - len(PATCHES)):
        header += struct.pack('<3I', *patch)
    header += b'\0' * (512 - len(header))
    struct.pack_into('<I', header, 16, sum32(bytes(header)))
    (out / 'payload.bin').write_bytes(header + data)
    return out
