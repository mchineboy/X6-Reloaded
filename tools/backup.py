"""FEL recovery image creation and read-only internal SPI NOR backups.

Image creation accesses ordinary host files only. Device commands are limited to
XFEL version, flash detection and flash reads; XFEL uses temporary RAM helpers.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

from build_module import find_linker
import sdimage
from verify_image import verify

ROOT = Path(__file__).resolve().parents[1]
SUNXI_COMMIT = 'a08a24a2aa5804bde7fa6ea8cdb08135d018da33'
HELPER_SHA = '824367333824a5dfa3aef8c6386c5ea22d11df2d135c9525a835b387957ff85d'
FLASH_BYTES = 8 * 1024 * 1024


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_fel(image):
    result = verify(image)
    with image.open('rb') as stream:
        stream.seek(8192)
        helper = stream.read(8192)
    if hashlib.sha256(helper).hexdigest() != HELPER_SHA:
        raise ValueError('FEL helper differs from the device-tested ARMv5 build')
    return result


def fel_image(root=ROOT):
    source = Path(os.environ.get('SUNXI_TOOLS') or root / 'local/sunxi-tools').resolve()
    # Read the pinned upstream object, not possibly modified working-tree code.
    assembly = subprocess.check_output(
        ['git', '-C', str(source), 'show', SUNXI_COMMIT + ':fel-sdboot.S'])
    out = root / 'build/fel'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'fel-sdboot.S').write_bytes(assembly)
    subprocess.run([os.environ.get('CLANG') or 'clang', '--target=arm-none-eabi',
                    '-march=armv5te', '-marm', '-c', str(out / 'fel-sdboot.S'),
                    '-o', str(out / 'helper.o')], check=True)
    subprocess.run([find_linker(os.environ.get('LD_LLD')), '--image-base=0', '-Ttext=0x20',
                    '--oformat=binary', str(out / 'helper.o'),
                    '-o', str(out / 'code.bin')], check=True)
    code = (out / 'code.bin').read_bytes()
    if len(code) != 40:
        raise ValueError('Unexpected FEL code length')
    helper = bytearray(8192)
    struct.pack_into('<I8sII4s', helper, 0, 0xea000006, b'eGON.BT0',
                     0x5f0a6c39, len(helper), b'SPL\x01')
    helper[32:72] = code
    struct.pack_into('<I', helper, 12, sum(struct.unpack('<2048I', helper)) & 0xffffffff)
    if hashlib.sha256(helper).hexdigest() != HELPER_SHA:
        raise ValueError('Compiled FEL helper differs from the device-tested build')
    (out / 'helper.sunxi').write_bytes(helper)
    (out / 'empty.bin').write_bytes(bytes(512))
    (out / 'README.TXT').write_text('X6 FEL recovery card. This is not the arcade card.\n')
    image = root / 'dist/X6-FEL.img'
    sdimage.build(image, {'README.TXT': out / 'README.TXT'},
                  out / 'helper.sunxi', out / 'empty.bin')
    image.with_name(image.name + '.sha256').write_text(f'{digest(image)}  {image.name}\n')
    verify_fel(image)
    (out / 'provenance.json').write_text(json.dumps({
        'source': 'https://github.com/linux-sunxi/sunxi-tools',
        'source_commit': SUNXI_COMMIT, 'architecture': 'ARMv5TE',
        'compile_flags': ['--target=arm-none-eabi', '-march=armv5te', '-marm'],
        'link_flags': ['--image-base=0', '-Ttext=0x20', '--oformat=binary'],
        'helper_bytes': len(helper), 'helper_sha256': HELPER_SHA,
        'sd_offset': 8192, 'image_sha256': digest(image),
        'device_tested_image': False,
    }, indent=2) + '\n')
    print(f'Created {image}; exact tested helper verified. No card was accessed.')


def identify(xfel):
    version = subprocess.check_output([xfel, 'version'], text=True, stderr=subprocess.STDOUT)
    if not re.search(r'\bID=0x00166300\b', version, re.IGNORECASE):
        raise ValueError('Expected F1C100S/F1C200S family ID=0x00166300: ' + version.strip())
    flash = subprocess.check_output([xfel, 'spinor'], text=True, stderr=subprocess.STDOUT)
    if not re.search(r"Found spi nor flash '[^'\r\n]+' with 8388608 bytes", flash):
        raise ValueError('Expected detected 8 MiB SPI NOR: ' + flash.strip())
    return {'version': version.strip(), 'flash': flash.strip()}


def backup(root=ROOT, xfel='xfel'):
    identity = identify(xfel)
    # Every attempt gets a new private directory, including failed attempts.
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    folder = root / 'local/backups' / stamp
    folder.mkdir(parents=True, mode=0o700, exist_ok=False)
    report = {'time_utc': stamp, 'identity': identity, 'size_bytes': FLASH_BYTES,
              'reads_identical': False, 'flash_program_or_erase_commands_used': False,
              'restore_test_performed': False, 'xfel_executable': xfel}
    manifest = folder / 'verification.json'
    manifest.write_text(json.dumps(report, indent=2) + '\n')
    try:
        paths = []
        for number in (1, 2):
            path = folder / f'internal-flash-read{number}.bin'
            print(f'Reading internal flash ({number}/2) into {path}', flush=True)
            result = subprocess.run([xfel, 'spinor', 'read', '0', hex(FLASH_BYTES), str(path)],
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            (folder / f'read{number}.log').write_text(result.stdout)
            result.check_returncode()
            if path.is_symlink() or not path.is_file() or path.stat().st_size != FLASH_BYTES:
                raise ValueError(f'Read {number} did not produce a regular 8 MiB backup')
            paths.append(path)
        hashes = [digest(path) for path in paths]
        report['read_sha256'] = hashes
        if hashes[0] != hashes[1]:
            raise ValueError('Flash reads differ; keep both files and retry with a stable connection')
        if paths[0].read_bytes() in (bytes(FLASH_BYTES), b'\xff' * FLASH_BYTES):
            raise ValueError('Uniform empty flash read; do not use this as firmware input')
        report.update(reads_identical=True, sha256=hashes[0],
                      backup_files=[path.name for path in paths])
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        report['error'] = str(error)
        raise
    finally:
        manifest.write_text(json.dumps(report, indent=2) + '\n')
        print(f'Backup record: {manifest}', flush=True)
    print(f'PASS: two matching 8 MiB reads. SHA-256: {hashes[0]}')
    print(f'Firmware input: {paths[0]}')
    print('Copy this directory to another drive. Matching reads are not a tested restore.')
    return folder


def main():
    command = sys.argv[1]
    xfel = os.environ.get('XFEL') or 'xfel'
    if command == 'fel-image':
        fel_image()
    elif command == 'fel-check':
        print(json.dumps(identify(xfel), indent=2))
    elif command == 'backup':
        backup(xfel=xfel)
    else:
        raise ValueError('Unknown backup command')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
