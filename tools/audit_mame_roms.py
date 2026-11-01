#!/usr/bin/env python3
"""Audit local ZIPs against five pinned 0.37b5 drivers; optionally rebuild copies.

Never extracts archive paths or edits source archives. Donkey Kong's three
known four-bit PROMs have a narrowly allowlisted legacy upper-nibble conversion.
This is a ROM byte audit, not proof of working emulation or sample completeness.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile
import zlib

PIN = '181b42e5da0d2f0c7a2e59244f23633ed594de2e'
DRIVERS = {'dkong': 'dkong.c', 'galaga': 'galaga.c', 'digdug': 'digdug.c',
           'rygar': 'tecmo.c', 'altbeast': 'system16.c'}
# Exact source CRC -> exact expected CRC. Never apply to arbitrary ROM data.
DK_PROM_CONVERSIONS = {
    ('dkong.2k', 0xe273ede5, 0x1e82d375),
    ('dkong.2j', 0xd6412358, 0x2ab01dc8),
    ('dkong.5f', 0xb869b8f5, 0x44988665),
}


def requirements(text, game):
    """Parse the small macro subset used by these five source definitions.

    CONTINUE consumes more bytes from the same file. RELOAD resets its cursor;
    it must not incorrectly double the required archive member length.
    Fail closed on unsupported ROM macros or expressions.
    """
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    block = re.search(r'\bROM_START\s*\(\s*' + re.escape(game)
                      + r'\s*\)(.*?)\bROM_END\b', text, re.S)
    if not block:
        raise ValueError('ROM definition not found: ' + game)
    result, current, cursor = [], None, 0
    for macro, args in re.findall(r'\b(ROM_\w+)\s*\(([^)]*)\)', block.group(1)):
        fields = [v.strip() for v in args.split(',')]
        if macro == 'ROM_REGION':
            current = None
        elif macro in {'ROM_LOAD', 'ROM_LOAD_EVEN', 'ROM_LOAD_ODD', 'ROM_LOAD_GFX_EVEN', 'ROM_LOAD_GFX_ODD'}:
            if len(fields) != 4 or not re.fullmatch(r'"[^"/\\]+"', fields[0]):
                raise ValueError('Unsupported ROM_LOAD arguments')
            cursor = int(fields[2], 0)
            crc = int(fields[3], 0)
            if cursor <= 0 or not 0 < crc <= 0xffffffff:
                raise ValueError('Missing/bad ROM checksum or length')
            current = {'name': fields[0][1:-1], 'bytes': cursor, 'crc32': crc}
            result.append(current)
        elif macro in {'ROM_CONTINUE', 'ROM_RELOAD'}:
            if current is None or len(fields) != 2:
                raise ValueError(macro + ' without a ROM_LOAD')
            length = int(fields[1], 0)
            if length <= 0:
                raise ValueError('Bad continuation length')
            cursor = cursor + length if macro == 'ROM_CONTINUE' else length
            current['bytes'] = max(current['bytes'], cursor)
        else:
            raise ValueError('Unsupported ROM macro: ' + macro)
    if not result or len({r['name'] for r in result}) != len(result):
        raise ValueError('Empty or duplicate ROM definition')
    return result


def locate_driver(upstream, game):
    """Find the pinned driver file whose GAME()/GAMEX() line defines `game`."""
    pattern = re.compile(r'\bGAMEX?\(\s*\d+\s*,\s*' + re.escape(game) + r'\s*,')
    hits = [f.name for f in sorted((upstream/'src/drivers').glob('*.c'))
            if pattern.search(f.read_text(errors='replace'))]
    if len(hits) != 1:
        raise SystemExit(f'Driver for {game} not found exactly once: {hits}')
    return hits[0]


def matching_bytes(archive, game, item):
    candidates = sorted((i for i in archive.infolist() if not i.is_dir()
                         and i.file_size == item['bytes']),
                        key=lambda i: (i.filename != item['name'], i.filename))
    for info in candidates:
        transform = 'identity'
        if info.CRC != item['crc32']:
            if (game != 'dkong' or info.file_size != 256 or
                    (item['name'], info.CRC, item['crc32']) not in DK_PROM_CONVERSIONS):
                continue
            transform = 'legacy-4bit-prom-upper-nibble-f0'
        data = archive.read(info)  # ZIP reader also verifies the stored member CRC.
        if zlib.crc32(data) != info.CRC:
            raise ValueError('ZIP member CRC mismatch')
        if transform != 'identity':
            if any(v & 0xf0 for v in data):
                raise ValueError('Unexpected upper bits in four-bit PROM')
            data = bytes(v | 0xf0 for v in data)
        if zlib.crc32(data) == item['crc32'] and len(data) == item['bytes']:
            return data, {'source_member': info.filename,
                          'source_crc32': f'{info.CRC:08x}', 'transform': transform}
    return None, {}


def audit(archive_path, source, game, output=None):
    requested = requirements(source.read_text(errors='replace'), game)
    rows, payloads = [], []
    with zipfile.ZipFile(archive_path) as archive:
        for item in requested:
            data, match = matching_bytes(archive, game, item)
            rows.append({**item, 'crc32': f"{item['crc32']:08x}",
                         'matched': data is not None, **match})
            if data is not None:
                payloads.append((item['name'], data))
    complete = all(r['matched'] for r in rows)
    report = {'game': game, 'driver_source': str(source), 'archive': str(archive_path),
              'archive_sha256': hashlib.sha256(archive_path.read_bytes()).hexdigest(),
              'required': len(rows), 'matched': len(payloads), 'complete': complete,
              'members': rows, 'x6_tested': False, 'samples_audited': False}
    if output is not None and complete:
        output = Path(output)
        if output.resolve() == archive_path.resolve():
            raise ValueError('Refusing to replace the source archive')
        output.parent.mkdir(parents=True, exist_ok=True)
        # Exclusive creation: never replace an existing private archive either.
        with output.open('xb') as handle:
            with zipfile.ZipFile(handle, 'w', zipfile.ZIP_DEFLATED) as rebuilt:
                for name, data in payloads:
                    info = zipfile.ZipInfo(name, (2000, 1, 1, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    rebuilt.writestr(info, data)
        with zipfile.ZipFile(output) as rebuilt:
            for item in requested:
                data = rebuilt.read(item['name'])
                if len(data) != item['bytes'] or zlib.crc32(data) != item['crc32']:
                    raise ValueError('Rebuilt archive verification failed')
        report.update(output=str(output), output_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
                      output_bytes=output.stat().st_size, output_verified=True)
    return report


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', type=Path, default=root/'local/upstream/mame2000-libretro')
    parser.add_argument('--archives', type=Path, required=True, help='directory with game-name ZIPs')
    parser.add_argument('--game', action='append',
                        help='driver name; games outside the original five are located by their GAME() line')
    parser.add_argument('--source-zip', action='append', default=[], metavar='GAME=NAME[,NAME]',
                        help='modern merged-set ZIP(s) to search when <game>.zip is absent or incomplete')
    parser.add_argument('--output-dir', type=Path, help='opt-in rebuild directory; refuses existing ZIPs')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=args.upstream, text=True).strip()
    if revision != PIN:
        raise SystemExit('Unexpected upstream revision; expected ' + PIN)
    reports = []
    sources = dict(item.split('=', 1) for item in args.source_zip)
    for game in args.game or list(DRIVERS):
        driver_file = DRIVERS.get(game) or locate_driver(args.upstream, game)
        source = args.upstream/'src/drivers'/driver_file
        tracked = subprocess.check_output(['git', 'show', 'HEAD:src/drivers/'+driver_file], cwd=args.upstream)
        if source.read_bytes() != tracked:
            raise SystemExit('Driver source differs from pinned commit: ' + str(source))
        out = args.output_dir/(game+'.zip') if args.output_dir else None
        names = sources.get(game, game).split(',')
        candidates = [args.archives/(n+'.zip') for n in names if (args.archives/(n+'.zip')).is_file()]
        if not candidates:
            reports.append({'game': game, 'complete': False, 'error': 'no source archive: ' + ', '.join(names)})
            print(f'{game}: no source archive ({", ".join(names)})')
            continue
        try:
            result = None
            for archive in candidates:
                result = audit(archive, source, game, out)
                if result['complete']:
                    break
        except ValueError as error:
            reports.append({'game': game, 'complete': False, 'error': str(error)})
            print(f'{game}: {error}')
            continue
        reports.append(result)
        print(f"{game}: {result['matched']}/{result['required']} ROMs match; "
              + ('private copy verified' if result.get('output_verified') else 'audit only'))
    data = {'date': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'pinned_commit': revision, 'originals_modified': False, 'games': reports}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(data, indent=2)+'\n')
    else:
        print(json.dumps(data, indent=2))
    return 0 if all(r['complete'] for r in reports) else 1


if __name__ == '__main__':
    raise SystemExit(main())
