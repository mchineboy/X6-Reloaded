"""Native sanitizer checks for the same session/video code linked into the host."""
from pathlib import Path
import subprocess
import argparse

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock-backup', type=Path, help='optional read-only native audit of a local stock backup')
    args = parser.parse_args()
    out = ROOT / 'build/stock-tests'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'session-test'
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'boot'),
                    str(ROOT / 'boot/session.c'), str(ROOT / 'tests/stock_session_host.c'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    browser = out / 'browser-test'
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I' + str(ROOT / 'include'), str(ROOT / 'src/ui/browser.c'),
                    str(ROOT / 'src/ui/library.c'), str(ROOT / 'tests/browser_host.c'), '-o', str(browser)], check=True)
    subprocess.run([str(browser)], check=True)
    discovery = out / 'discovery-test'
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I' + str(ROOT / 'include'), str(ROOT / 'src/arcade/catalog.c'),
                    str(ROOT / 'src/runtime/directory.c'), str(ROOT / 'src/arcade/stock_catalog.c'),
                    str(ROOT / 'tests/discovery_host.c'), '-o', str(discovery)], check=True)
    subprocess.run([str(discovery)], check=True)
    if args.stock_backup:
        native = out / 'stock-backup-test'
        subprocess.run(['clang', '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-I' + str(ROOT / 'include'), str(ROOT / 'src/runtime/directory.c'),
                        str(ROOT / 'src/arcade/stock_catalog.c'), str(ROOT / 'tests/stock_backup_host.c'),
                        '-o', str(native)], check=True)
        subprocess.run([str(native), str(args.stock_backup)], check=True)
    core = ROOT / 'local/mame2000/src'
    if not (core / 'driver.h').is_file():
        print('SKIP frontend stock tests: run make deps to supply the pinned core headers')
        return
    binary = out / 'frontend-test'
    sources = ['src/arcade/frontend.c', 'src/arcade/catalog.c', 'src/arcade/timing.c', 'src/ui/browser.c', 'src/ui/library.c',
               'src/arcade/monitor.c', 'src/arcade/music.c', 'src/ui/gfx.c', 'src/ui/menu.c',
               'src/ui/font_data.c', 'tests/stock_frontend_host.c']
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DX6_TIMING_TEST',
                    '-I' + str(ROOT / 'include'), '-I' + str(core), '-I' + str(core / 'libretro'),
                    '-I' + str(core / 'libretro/libretro-common/include'),
                    *[str(ROOT / s) for s in sources], '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
