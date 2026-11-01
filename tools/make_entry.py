"""Pass Make variables as data, never as interpolated shell commands."""
import os
from pathlib import Path
import subprocess
import sys

if sys.argv[1] == 'help':
    print('X6 Reloaded\n\n'
          'make menuconfig ROMS=/path/to/roms FIRMWARE=/path/to/backup.bin TOOLCHAIN=/path/to/arm-gnu\n'
          'make image     Build a private, bootable SD image\n'
          'make verify-image   Check the image checksum and MBR/FAT32 layout (no card access)\n'
          'make sdcard DISK=/dev/diskN   Verify target, confirm erasure, write SD (macOS)\n'
          'Windows card creation: see WINDOWS.md (Imager or Etcher; WSL build route)\n'
          'make scan      Show compatible games\n'
          'make preview   Render original UI previews\n'
          'make check     Run source and local tests\n\n'
          'Optional: GAMES=all or GAMES=dkong,galaga; SAMPLES=/path/to/samples;\n'
          'CORE_SOURCE=/path/to/clean/pinned/mame2000; CLANG=clang; LD_LLD=/path/to/ld.lld\n'
          'Configuration is saved locally. No ROM or firmware downloads.')
else:
    command = [sys.executable, str(Path(__file__).with_name('reloaded.py')), sys.argv[1]]
    for var, flag in [('ROMS', 'roms'), ('FIRMWARE', 'firmware'), ('TOOLCHAIN', 'toolchain'),
                      ('SAMPLES', 'samples'), ('GAMES', 'games'), ('CORE_SOURCE', 'core-source'),
                      ('CLANG', 'cc'), ('LD_LLD', 'ld')]:
        if os.environ.get(var):
            command += ['--' + flag, os.environ[var]]
    raise SystemExit(subprocess.call(command))
