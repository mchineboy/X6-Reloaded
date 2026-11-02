#!/usr/bin/env python3
"""X6 Reloaded's local configure, ROM selection, and SD image workflow."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

from core import ROOT, UPSTREAM, prepare
from audit_mame_roms import locate_driver, requirements, matching_bytes, audit
import firmware
import sdimage
import stock

CONFIG = ROOT / '.config.json'


def load_config():
    if not CONFIG.is_file():
        raise ValueError('Run make menuconfig ROMS=/path/to/roms FIRMWARE=/path/to/backup.bin TOOLCHAIN=/path/to/toolchain first')
    return json.loads(CONFIG.read_text())


def inspect_game(game, folder):
    source = UPSTREAM / 'src/drivers' / locate_driver(UPSTREAM, game)
    archive = folder / (game + '.zip')
    if not archive.is_file():
        return {'game': game, 'available': False, 'reason': 'archive absent'}
    try:
        result = audit(archive, source, game)
        return {'game': game, 'available': result['complete'],
                'reason': 'ready' if result['complete'] else f"{result['matched']}/{result['required']} matching ROMs",
                'source': str(source)}
    except (ValueError, zipfile.BadZipFile, OSError) as error:
        return {'game': game, 'available': False, 'reason': str(error)}


def scan(folder):
    profile = json.loads((ROOT / 'config/mame2000-arcade.json').read_text())
    return [inspect_game(game, folder) for game in profile['drivers']]


def configure(args):
    previous = json.loads(CONFIG.read_text()) if CONFIG.exists() else {}
    for key in ['roms', 'firmware', 'toolchain', 'samples']:
        value = getattr(args, key) or previous.get(key)
        if value:
            previous[key] = str(Path(value).expanduser().resolve())
    for key in ['roms', 'firmware', 'toolchain']:
        if not previous.get(key):
            raise ValueError('Missing ' + key.upper() + '; supply it to make menuconfig')
    firmware.extract_stock(Path(previous['firmware']))
    if args.stock_backup:
        if str(args.stock_backup) == 'none':
            previous.pop('stock_backup', None)
        else:
            stock.inspect(args.stock_backup)
            previous['stock_backup'] = str(args.stock_backup.expanduser().resolve())
    if not (Path(previous['toolchain']) / 'bin/arm-none-eabi-gcc').is_file():
        raise ValueError('TOOLCHAIN must contain bin/arm-none-eabi-gcc')
    prepare(args.core_source)
    rows = scan(Path(previous['roms']))
    ready = [r['game'] for r in rows if r['available']]
    if not ready and args.games != 'none':
        for row in rows:
            print(row['game'] + ': ' + row['reason'])
        raise ValueError('No compatible games found; these must match MAME 0.37b5 checksums')
    if args.games:
        selected = [] if args.games == 'none' else args.games.split(',') if args.games != 'all' else ready
        if len(set(selected)) != len(selected) or not set(selected) <= set(ready):
            raise ValueError('GAMES must contain unique available short names, all, or none')
    else:
        if not sys.stdin.isatty():
            raise ValueError('menuconfig needs a terminal; use GAMES=all or GAMES=dkong,galaga for scripts')
        selected = [g for g in previous.get('games', ready) if g in ready]
        while True:
            print('\nX6 Reloaded / ROM selection\n')
            for n, row in enumerate(rows, 1):
                mark = 'x' if row['game'] in selected else ' '
                print(f"{n:2}. [{mark}] {row['game']:<10} {row['reason']}")
            answer = input('\nNumbers toggle; a = all available; n = none; s = save; q = cancel: ').strip().lower()
            if answer == 'q':
                return
            if answer == 's':
                break
            if answer == 'a':
                selected = ready.copy()
            elif answer == 'n':
                selected = []
            else:
                for token in answer.replace(',', ' ').split():
                    if not token.isdigit() or not 1 <= int(token) <= len(rows):
                        print('Choose one of the listed numbers.')
                        continue
                    game = rows[int(token) - 1]['game']
                    if game not in ready:
                        print(game + ' is unavailable; no mystery meat goes into this build.')
                    elif game in selected:
                        selected.remove(game)
                    else:
                        selected.append(game)
    if not selected and args.games != 'none':
        raise ValueError('Select at least one compatible game')
    previous['games'] = [g for g in ready if g in selected]
    CONFIG.write_text(json.dumps(previous, indent=2) + '\n')
    print(f"Saved {len(selected)} games to .config.json (private, ignored by Git)")


def build(args):
    config = load_config()
    stock_backup = args.stock_backup if args.stock_backup is not None else config.get('stock_backup')
    if str(stock_backup) == 'none':
        stock_backup = None
    if stock_backup:
        stock.inspect(stock_backup)
    prepare(args.core_source)
    roms = Path(config['roms'])
    # Re-audit the selection before invoking a compiler or touching outputs.
    for game in config['games']:
        row = inspect_game(game, roms)
        if not row['available']:
            raise ValueError(game + ': ' + row['reason'])
    manifest = json.loads((ROOT / 'config/mame2000-arcade.json').read_text())
    # Selection controls image contents, never the emulator's supported pool.
    out = ROOT / 'build'
    out.mkdir(exist_ok=True)
    profile = out / 'supported.json'
    profile.write_text(json.dumps(manifest, indent=2) + '\n')
    command = [sys.executable, str(ROOT / 'tools/build_module.py'), '--manifest', str(profile),
               '--toolchain', config['toolchain'], '--fast', '--altbeast-target-rate',
               '--game-samples', '--area-scale', '--galaga-c-z80', '--timing-calibration',
               '--build-dir', str(out / 'module'), '--cc', args.cc]
    if args.ld:
        command += ['--ld', args.ld]
    subprocess.run(command, check=True)
    boot = firmware.build(ROOT, Path(config['firmware']), Path(config['toolchain']))
    # A new staging directory avoids stale ROMs surviving deselection.
    staging = out / 'sd-files'
    if staging.exists():
        shutil.rmtree(staging)
    (staging / 'game/cps/samples').mkdir(parents=True)
    (staging / 'roms/mame/samples').mkdir(parents=True)
    (staging / 'roms/original').mkdir(parents=True)
    for name in ('nes', 'snes', 'md', 'gb', 'gbc', 'gba'):
        folder = staging / 'roms' / name
        folder.mkdir(parents=True)
        (folder / 'README.TXT').write_text(
            'Copy compatible ' + name.upper() + ' ROM files here, safely eject and restart the X6.\n'
            'The supplied original emulator is required under x6/stock/. Console playback needs device testing.\n'
            'Use x6/titles.tsv for display-title overrides; do not rename ROMs or saves.\n')
    (staging / 'res').mkdir(parents=True)
    (staging / 'res/README.TXT').write_text('The original GBA emulator uses this folder for its cache files.\n')
    (staging / 'roms/mame/README.TXT').write_text(
        'Copy supported MAME 2000 / 0.37b5 game-name ZIPs here, then reboot.\n'
        'Samples go in the samples subfolder. Unsupported drivers need a core update.\n'
        'Other ROM files appear grey with a reason and cannot launch.\n'
        'Supported short names: ' + ', '.join(manifest['drivers']) + '\n')
    (staging / 'roms/original/README.TXT').write_text(
        'Copy compatible original-X6 .fba packs or original-FBA game-name ZIPs here, then reboot.\n'
        'Requires your original FBA module under x6/stock/fba.mod.\n'
        'Legacy game/mame packed games and x6/stock trial games are also discovered.\n'
        'Invalid files and unsupported formats appear grey and cannot launch.\n'
        'Recognized headers are not a compatibility guarantee: ROM set unverified.\n')
    (staging / 'x6').mkdir(parents=True)
    shutil.copyfile(out / 'module/x6-dkong.experimental.elf', staging / 'x6/arcade.mod')
    shutil.copyfile(boot / 'x6menu.axf', staging / 'x6/x6menu.axf')
    script = r'''insmod \\drv\\matrixkey.drv
run f:\\x6\\x6menu.axf /w
startx
'''.replace('\n', '\r\n').encode('ascii')
    (staging / 'x6/boot.esh').write_bytes(script)
    stock_report = stock.stage(stock_backup, staging) if stock_backup else None
    title_index = getattr(args, 'title_index', None)
    if title_index:
        content = Path(title_index).read_bytes()
        if len(content) >= 524288 or len(content.splitlines()) > 8193:
            raise ValueError('Title index exceeds runtime limits')
        (staging / 'x6/titles.tsv').write_bytes(content)
    reports = []
    for game in config['games']:
        source = UPSTREAM / 'src/drivers' / locate_driver(UPSTREAM, game)
        reports.append(audit(roms / (game + '.zip'), source, game, staging / 'roms/mame' / (game + '.zip')))
    samples = Path(config.get('samples') or roms / 'samples')
    # Samples are optional local recordings, never fetched. Keep the explicit list
    # small; filenames are what these pinned drivers actually request.
    sample_names = {'dkong': ['effect00.wav', 'effect01.wav', 'effect02.wav'],
                    'dkongjr': ['jump.wav', 'land.wav', 'roar.wav', 'climb.wav', 'death.wav', 'drop.wav', 'walk.wav', 'snapjaw.wav'],
                    'galaga': ['bang.wav', 'init.wav'], 'xevious': ['explo1.wav', 'explo2.wav'],
                    'galaxian': ['shot.wav', 'death.wav'],
                    'bosco': ['midbang.wav', 'bigbang.wav', 'shot.wav']}
    sample_report = []
    for game in config['games']:
        if game not in sample_names:
            continue
        source = samples / (game + '.zip')
        if source.is_file():
            with zipfile.ZipFile(source) as archive:
                requested = sample_names[game]
                names = {Path(n).name.lower(): n for n in archive.namelist()}
                missing = [n for n in requested if n not in names]
                target = staging / 'roms/mame/samples' / (game + '.zip')
                with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as output:
                    for name in requested:
                        if name in names:
                            info = zipfile.ZipInfo(name, (2000, 1, 1, 0, 0, 0))
                            info.compress_type = zipfile.ZIP_DEFLATED
                            output.writestr(info, archive.read(names[name]))
                sample_report.append({'game': game, 'missing': missing})
                if missing:
                    print('Sample warning: ' + game + ' missing ' + ', '.join(missing))
        else:
            print('Sample warning: optional ' + str(source) + ' absent; sound may be incomplete')
            sample_report.append({'game': game, 'missing': sample_names[game]})
    files = {str(p.relative_to(staging)): p for p in staging.rglob('*') if p.is_file()}
    target = ROOT / 'dist/X6-Reloaded.img'
    geometry = sdimage.build(target, files, boot / 'stage.bin', boot / 'payload.bin')
    with target.open('rb') as stream:
        checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
    (ROOT / 'dist/X6-Reloaded.img.sha256').write_text(checksum + '  X6-Reloaded.img\n')
    (out / 'image-report.json').write_text(json.dumps({'geometry': geometry, 'roms': reports,
        'samples': sample_report, 'stock_trial': stock_report,
        'selected_games': config['games'], 'compiled_drivers': manifest['drivers'],
        'image_sha256': checksum, 'hardware_tested': False,
        'firmware_sha256': hashlib.sha256(Path(config['firmware']).read_bytes()).hexdigest()}, indent=2) + '\n')
    print('\nBuilt ' + str(target))
    print('Local build complete. No card or internal firmware was written.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['menuconfig', 'image', 'scan', 'deps'])
    for name in ['roms', 'firmware', 'toolchain', 'samples', 'core-source']:
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--games')
    parser.add_argument('--stock-backup', type=Path,
                        help='optional original-card backup for arcade/console libraries; none disables it')
    parser.add_argument('--title-index', type=Path, help='optional private display-title TSV; ROM names stay unchanged')
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--ld')
    args = parser.parse_args()
    try:
        if args.action == 'deps':
            prepare(args.core_source)
        elif args.action == 'menuconfig':
            configure(args)
        elif args.action == 'scan':
            prepare(args.core_source)
            folder = args.roms or Path(load_config()['roms'])
            for row in scan(folder):
                print(row['game'] + ': ' + row['reason'])
        else:
            build(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')

if __name__ == '__main__':
    main()
