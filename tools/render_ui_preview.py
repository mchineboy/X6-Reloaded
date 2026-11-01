#!/usr/bin/env python3
"""Build and run the menu host tests, then write PNG previews to build/ui-preview.

Previews come from host-side test fixtures rendered at 320x240 and enlarged
3x with nearest-neighbour scaling. They are not photographs of the X6 panel,
whose physical scaling of the 320x240 surface is still unobserved.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import zlib


def ppm_to_png(source, target, scale):
    data = source.read_bytes()
    header, rest = data.split(b'\n', 3)[:3], data.split(b'\n', 3)[3]
    if header[0] != b'P6' or header[2] != b'255':
        raise ValueError(f'unexpected PPM header in {source}')
    width, height = map(int, header[1].split())
    rows = []
    for y in range(height):
        line = rest[y * width * 3:(y + 1) * width * 3]
        wide = b''.join(line[x * 3:x * 3 + 3] * scale for x in range(width))
        rows.extend([b'\0' + wide] * scale)

    def chunk(kind, payload):
        body = kind + payload
        return struct.pack('>I', len(payload)) + body + struct.pack('>I', zlib.crc32(body))

    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', width * scale, height * scale, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(b''.join(rows), 9))
           + chunk(b'IEND', b''))
    target.write_bytes(png)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--scale', type=int, default=3)
    parser.add_argument('--no-png', action='store_true', help='run tests only')
    args = parser.parse_args()
    subprocess.run(['python3', str(root / 'tools/gen_font.py'), '--check'], check=True)
    out = root / 'build/ui-preview'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'ui-host-test'
    sources = ['src/ui/gfx.c', 'src/ui/menu.c', 'src/ui/splash.c', 'src/ui/font_data.c', 'tests/ui_host.c']
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I' + str(root / 'include'), *[str(root / s) for s in sources],
                    '-o', str(binary)], check=True)
    result = subprocess.run([str(binary), str(out)], check=True, capture_output=True, text=True)
    print(result.stdout, end='')
    if args.no_png:
        return
    for ppm in sorted(out.glob('*.ppm')):
        ppm_to_png(ppm, ppm.with_suffix('.png'), args.scale)
        ppm.unlink()
        print(f'Preview: {ppm.with_suffix(".png").relative_to(root)}')


if __name__ == '__main__':
    main()
