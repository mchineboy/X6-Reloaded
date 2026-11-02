"""Pass Make variables as data, never as interpolated shell commands."""
import os
from pathlib import Path
import subprocess
import sys

if sys.argv[1] == 'help':
    print('X6 Reloaded\n\n'
          'Start-to-finish SD setup: SETUP.md\n'
          'Start here for a first backup: BACKUP.md\n'
          'make fel-image   Build a FEL recovery SD image (no firmware/ROM input)\n'
          'make fel-sdcard DISK=/dev/diskN   Confirm erasure, write FEL SD (macOS)\n'
          'make fel-check   Identify an already connected FEL unit and 8 MiB SPI NOR\n'
          'make backup      Read internal flash twice; compare and keep both backups\n\n'
          'make menuconfig ROMS=/path/to/roms FIRMWARE=/path/to/backup.bin TOOLCHAIN=/path/to/arm-gnu\n'
          'make image     Build a private, bootable SD image\n'
          'make verify-image   Check the image checksum and MBR/FAT32 layout (no card access)\n'
          'make sdcard DISK=/dev/diskN   Verify target, confirm erasure, write SD (macOS)\n'
          'Windows card creation: see WINDOWS.md (Imager or Etcher; WSL build route)\n'
          'make scan      Show compatible games\n'
          'make preview   Render original UI previews\n'
          'make check     Run source and local tests\n\n'
          'Optional: GAMES=all, GAMES=dkong,galaga, or GAMES=none (add ROMs later); SAMPLES=/path/to/samples;\n'
          'STOCK_BACKUP=/path/to/stock-card-backup imports original modules and libraries privately;\n'
          'TITLE_INDEX=/path/to/titles.tsv adds display titles without renaming ROMs;\n'
          'CORE_SOURCE=/path/to/clean/pinned/mame2000; CLANG=clang; LD_LLD=/path/to/ld.lld\n'
          'Configuration is saved locally. No ROM or firmware downloads.')
else:
    command = [sys.executable, str(Path(__file__).with_name('reloaded.py')), sys.argv[1]]
    for var, flag in [('ROMS', 'roms'), ('FIRMWARE', 'firmware'), ('TOOLCHAIN', 'toolchain'),
                      ('SAMPLES', 'samples'), ('GAMES', 'games'), ('CORE_SOURCE', 'core-source'),
                      ('CLANG', 'cc'), ('LD_LLD', 'ld'), ('TITLE_INDEX', 'title-index')]:
        if os.environ.get(var):
            command += ['--' + flag, os.environ[var]]
    if os.environ.get('STOCK_BACKUP'):
        command += ['--stock-backup', os.environ['STOCK_BACKUP']]
    raise SystemExit(subprocess.call(command))
