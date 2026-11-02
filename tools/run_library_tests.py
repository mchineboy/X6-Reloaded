"""Native sanitized production-library audit; only temporary cache files are written."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import stock_metadata

ROOT = Path(__file__).resolve().parents[1]


def write(root, relative, content):
    p = root / relative
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(content)


def fixtures(root):
    for folder in ('fc', 'nes'):
        for i in range(3000):
            write(root, f'game/{folder}/{i:04d}. Game {folder} {i:04d}.nes', b'NES\x1a' + bytes(12))
    write(root, 'roms/nes/broken.nes', b'invalid header')
    write(root, 'roms/nes/compressed.zip', b'unsupported archive')
    write(root, 'game/md/misplaced.nes', b'NES\x1a' + bytes(12))
    for name, ident in [('Diplodocus', 'doubledr'), ('2020bb', '2020bb')]:
        write(root, f'game/mame/{name}.fba', b'\x71\x96\x02\x00' + ident.encode().ljust(12, b'\0') + bytes(16))
    for folder, ext, title in [('sfc', 'sfc', b'CONTRA3 THE ALIEN WAR'), ('smc', 'smc', b'REAL MONSTERS')]:
        rom = bytearray(32768 + (512 if ext == 'smc' else 0))
        at = 0x7fc0 + (512 if ext == 'smc' else 0)
        rom[at:at+21] = title.ljust(21, b' ')
        rom[at+28:at+32] = b'\x34\x12\xcb\xed'
        write(root, f'game/{folder}/001 Cryptic.{ext}', rom)
    for folder, ext, title in [('gb', 'gb', b'DUCK TALES'), ('gbc', 'gbc', b'DW1&2')]:
        rom = bytearray(512);rom[0x134:0x134+len(title)] = title
        if folder == 'gbc':rom[0x143] = 0x80
        write(root, f'game/{folder}/001 Cryptic.{ext}', rom)
    rom = bytearray(512);rom[0xa0:0xac] = b'KURURIN'.ljust(12, b'\0');rom[0xb2] = 0x96
    write(root, 'game/gba/001 Cryptic.gba', rom)
    rom = bytearray(16384);rom[0x100:0x104] = b'SEGA';rom[0x150:0x180] = b'SPLATTER HOUSE 2'.ljust(48, b' ')
    write(root, 'game/md/001 Haunted house2.bin', rom)
    smd = bytearray(512 + 16384)
    smd[512:512+8192] = rom[1::2];smd[512+8192:] = rom[::2]
    write(root, 'game/smd/001 Cryptic.smd', smd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock-backup', type=Path)
    parser.add_argument('--title-index', type=Path)
    args = parser.parse_args()
    out = ROOT / 'build/stock-tests';out.mkdir(parents=True, exist_ok=True)
    binary = out / 'libraries-test'
    sources = ['src/arcade/libraries.c', 'src/arcade/stock_catalog.c', 'src/arcade/titles.c',
               'src/runtime/directory.c', 'src/ui/library.c', 'src/ui/browser.c',
               'tests/library_filesystem.c', 'tests/libraries_host.c']
    subprocess.run(['clang', '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I'+str(ROOT/'include'),
                    *[str(ROOT/s) for s in sources], '-o', str(binary)], check=True)
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp);source = args.stock_backup or temp/'input';output = temp/'output'
        (output/'x6/stock').mkdir(parents=True)
        if args.stock_backup:
            for name in ('fba', 'fc', 'sfc', 'md', 'gbc', 'gba'):
                shutil.copyfile(source/f'apps/mod/{name}.mod', output/f'x6/stock/{name}.mod')
            titles = stock_metadata.extract(source/'apps/mod/fba.mod')
        else:
            fixtures(source)
            for name in ('fba', 'fc', 'sfc', 'md', 'gbc', 'gba'):
                write(output, f'x6/stock/{name}.mod', b'test module; never executed')
            titles = 'doubledr\tDouble Dragon (Neo-Geo)\t1995\tTechnos\n2020bb\t2020 Super Baseball\t1991\tSNK\n'
            write(output, 'x6/titles.tsv', b'game/nes/2999. Game nes 2999.nes\tZelda friendly override\n')
        (output/'x6/stock/arcade-titles.tsv').write_text(titles)
        if args.title_index:
            shutil.copyfile(args.title_index, output/'x6/titles.tsv')
        subprocess.run([str(binary), str(source), str(output), *(['audit'] if args.stock_backup else [])], check=True)


if __name__ == '__main__':
    main()
