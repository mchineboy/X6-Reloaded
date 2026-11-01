#!/usr/bin/env python3
"""Write a previously built image to an explicitly confirmed removable SD card.

macOS only. This erases the target card. It never discovers a target for you,
repartitions the computer, or invokes an X6 firmware/flash updater.
"""
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import stat
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def inspect(disk):
    return plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', disk]))


def validate(disk, info, image_size):
    if not re.fullmatch(r'/dev/disk[1-9][0-9]*', disk):
        raise ValueError('Choose an explicit whole removable disk, e.g. /dev/disk4. disk0 is refused.')
    if not info.get('WholeDisk') or not info.get('RemovableMedia') or not info.get('WritableMedia'):
        raise ValueError('Target must be a whole, writable, removable disk')
    if info.get('VirtualOrPhysical') != 'Physical':
        raise ValueError('Target must be physical removable media')
    bus = info.get('BusProtocol')
    if bus not in ['USB', 'Secure Digital'] or (info.get('Internal') and bus != 'Secure Digital'):
        raise ValueError('Only removable USB/SD media is supported')
    size = info.get('TotalSize', 0)
    if size < image_size or size > 256 * 1024**3:
        raise ValueError('Card capacity must fit the image and be no larger than 256 GiB')
    identity = {k: info.get(k) for k in ['DeviceIdentifier', 'DeviceTreePath', 'MediaName',
                'TotalSize', 'BusProtocol', 'RemovableMedia', 'Internal', 'MediaUUID']}
    if not identity['DeviceTreePath']:
        raise ValueError('No stable device-tree identity; refusing target')
    return identity


def main():
    if sys.platform != 'darwin':
        raise ValueError('make sdcard currently supports macOS. On other hosts, write dist/X6-Reloaded.img with a disk imager.')
    disk = os.environ.get('DISK', '')
    if not re.fullmatch(r'/dev/disk[1-9][0-9]*', disk):
        raise ValueError('Run diskutil list first, then supply DISK=/dev/diskN. No target is selected automatically.')
    image = ROOT / 'dist/X6-Reloaded.img'
    if not image.is_file() or image.is_symlink():
        raise ValueError('Run make image first')
    expected = (ROOT / 'dist/X6-Reloaded.img.sha256').read_text().split()[0]
    with image.open('rb') as stream:
        actual = hashlib.file_digest(stream, 'sha256').hexdigest()
    if actual != expected:
        raise ValueError('Image checksum differs; rebuild before writing')
    identity = validate(disk, inspect(disk), image.stat().st_size)
    print(json.dumps(identity, indent=2))
    print('\nTHIS ERASES THE ENTIRE SELECTED SD CARD. Back up its contents first.')
    if os.geteuid() != 0:
        raise ValueError('Writing needs root. After checking the identity above, run: sudo make sdcard DISK=' + disk)
    phrase = f"ERASE {disk} {identity['TotalSize']}"
    if input('\nType exactly ' + phrase + ': ') != phrase:
        raise ValueError('Cancelled; no writes performed')
    subprocess.run(['diskutil', 'unmountDisk', disk], check=True)
    if validate(disk, inspect(disk), image.stat().st_size) != identity:
        raise ValueError('Card identity changed after unmount; no writes performed')
    raw = '/dev/r' + disk.removeprefix('/dev/')
    # Keep the opened device for the full write/readback transaction.
    with open(raw, 'r+b', buffering=0) as target, image.open('rb') as incoming:
        if not stat.S_ISCHR(os.fstat(target.fileno()).st_mode):
            raise ValueError('Expected a macOS raw character device')
        while chunk := incoming.read(1024 * 1024):
            if target.write(chunk) != len(chunk):
                raise ValueError('Short device write')
        os.fsync(target.fileno())
        target.seek(0)
        readback, left = hashlib.sha256(), image.stat().st_size
        while left:
            chunk = target.read(min(left, 1024 * 1024))
            if not chunk:
                raise ValueError('Short device readback')
            readback.update(chunk)
            left -= len(chunk)
        if readback.hexdigest() != expected:
            raise ValueError('SD readback checksum differs; do not boot this card')
    report = ROOT / 'build/sd-write.json'
    report.write_text(json.dumps({'target': identity, 'image_sha256': expected,
                                  'verified': True}, indent=2) + '\n')
    subprocess.run(['diskutil', 'eject', disk], check=True)
    print('SD image verified and ejected. Insert it into the powered-off X6; unplug USB before booting.')

if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError, EOFError) as error:
        raise SystemExit(str(error))
