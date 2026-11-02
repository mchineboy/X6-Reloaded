"""Identify NES ROMs by a supplied DAT's size, CRC32 and SHA1; never rename ROMs.

Accepts full-file and iNES PRG/CHR payload matches. Ambiguous or changed ROMs
keep their filename fallback. This tool downloads nothing. DATs and generated
title indexes are private inputs/outputs; no database is bundled with the code.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zlib


def database(path):
    data = Path(path).read_text(encoding='utf-8-sig')
    result = {}
    for match in re.finditer(r'game\s*\(\s*name\s+"((?:\\.|[^"\\])*)"(.*?)\n\)', data, re.S):
        title = match[1].replace('\\"', '"').replace('\\\\', '\\')
        for rom in re.finditer(r'rom\s*\(\s*name\s+"(?:\\.|[^"\\])*"(.*?)\)', match[2], re.S):
            size = re.search(r'\bsize\s+(\d+)', rom[1])
            crc = re.search(r'\bcrc\s+([0-9a-fA-F]{8})', rom[1])
            sha = re.search(r'\bsha1\s+([0-9a-fA-F]{40})', rom[1])
            if size and crc and sha:
                key = (int(size[1]), int(crc[1], 16), sha[1].lower())
                result.setdefault(key, set()).add(title)
    if not result:
        raise ValueError('DAT contains no size/CRC32/SHA1 records')
    return result


def identify(data, entries):
    candidates = [data]
    if len(data) >= 16 and data[:4] == b'NES\x1a':
        prg, chr_ = data[4], data[5]
        if data[7] & 12 == 8:
            if data[9] & 15 != 15 and data[9] >> 4 != 15:
                prg |= (data[9] & 15) << 8
                chr_ |= (data[9] >> 4) << 8
            else:
                prg = chr_ = 0  # Exponential NES2 sizes: full-file match only.
        start = 16 + (512 if data[6] & 4 else 0)
        size = prg * 16384 + chr_ * 8192
        if size and start + size <= len(data):
            candidates.append(data[start:start+size])
    for candidate in candidates:
        key = (len(candidate), zlib.crc32(candidate), hashlib.sha1(candidate).hexdigest())
        names = entries.get(key, set())
        if len(names) == 1:
            return next(iter(names))
    return None


def generate(root, dat, output):
    root = Path(root).resolve(strict=True);output = Path(output).resolve()
    if output == root or root in output.parents:
        raise ValueError('Write the title index outside the source ROM backup')
    entries = database(dat);rows = [];unmatched = [];total = 0
    directories = []
    for base in (root/'game', root/'roms'):
        if base.is_dir():
            directories.extend(p for p in base.iterdir() if p.is_dir() and not p.is_symlink())
    for directory in sorted(directories):
        for path in sorted(directory.iterdir()):
            if path.suffix.lower() != '.nes' or path.name.startswith('.'):
                continue
            if path.is_symlink() or not path.is_file():
                raise ValueError('Expected regular NES file')
            total += 1
            relative = path.relative_to(root).as_posix()
            title = identify(path.read_bytes(), entries)
            if title and not any(c in relative + title for c in '\t\r\n'):
                rows.append(relative + '\t' + title)
            else:
                unmatched.append(relative)
    content = '# Relative SD path\tDisplay title; matched by DAT size, CRC32 and SHA1\n' + '\n'.join(rows) + '\n'
    if len(content.encode('utf-8')) >= 524288 or len(rows) > 8192:
        raise ValueError('Title index exceeds the runtime bound; split the library')
    output.parent.mkdir(parents=True, exist_ok=True);output.write_text(content, encoding='utf-8')
    return {'files_scanned': total, 'titles_matched': len(rows), 'unmatched': unmatched,
            'dat_sha256': hashlib.sha256(Path(dat).read_bytes()).hexdigest(), 'output': str(output),
            'roms_renamed_or_modified': False}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rom-root', type=Path, required=True)
    p.add_argument('--dat', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--report', type=Path)
    args = p.parse_args();report = generate(args.rom_root, args.dat, args.output)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f"Matched {report['titles_matched']}/{report['files_scanned']} files; ROMs unchanged.")


if __name__ == '__main__':
    main()
