"""Import the user's original arcade/console modules and libraries privately.

Modules and the initial 2020 seed have exact inspected checksum gates. Other
stock ROMs retain their filenames. No downloads, save-file import, device I/O,
or input modifications. Metadata is extracted from the user's original module.
"""
import hashlib
from pathlib import Path
import shutil
import stat
import stock_metadata

FILES = {
    'apps/mod/fba.mod': ('fba.mod', 2259132, '98d5b12acbef39a0b3b9ec4c367c7cebd8a975b4391472fa763090d4aca6b759'),
    'game/mame/2020bb.fba': ('2020bb.fba', 8889600, '07a39676091dea99883339e61656bb3fa4c53a092af64217490f394fac7b45d1'),
}
CONSOLES = {
    'fc.mod': (105644, '9b496cd85d0c061174007b053cc954b4dd49793795c771fca5a1023187d96bc1'),
    'sfc.mod': (297240, '62c93343f7d8c384f61eb4e5f0245f3fc76bac7631f66aad2d568021505a2894'),
    'md.mod': (607036, 'd36a042276595b018b523d18787ca9a18ff9ddc4b339f8e3e7b9a277e2a3d7ff'),
    'gbc.mod': (63368, '845854690fd2477328469a8009fdc0df21fe1777ce5972aa7de0edc150a25b8d'),
    'gba.mod': (814320, '828a12525f6eebd0d64b8848add4ea088b5ba81ec0f0cd92de411e221db822eb'),
}
ROM_FOLDERS = {'mame': {'.fba'}, 'fc': {'.nes'}, 'nes': {'.nes'}, 'sfc': {'.sfc'},
               'smc': {'.smc'}, 'md': {'.bin', '.nes'}, 'smd': {'.smd'}, 'gb': {'.gb'},
               'gbc': {'.gbc'}, 'gba': {'.gba', '.gbc'}}


def inspect(backup):
    backup = Path(backup).expanduser().resolve(strict=True)
    if not backup.is_dir():
        raise ValueError('STOCK_BACKUP must be a local stock-card backup directory')
    result = []
    inputs = dict(FILES)
    for name, (size, checksum) in CONSOLES.items():
        if (backup / 'apps/mod' / name).exists():
            inputs['apps/mod/' + name] = (name, size, checksum)
    for relative, (target, size, checksum) in inputs.items():
        path = backup / relative
        if any(p.is_symlink() for p in [path, *path.parents] if p != backup.parent):
            raise ValueError('Stock trial inputs must not contain symlinks: ' + relative)
        if not stat.S_ISREG(path.stat().st_mode):
            raise ValueError('Stock trial input must be a regular file: ' + relative)
        if path.stat().st_size != size:
            raise ValueError('Unsupported stock trial file size: ' + relative)
        with path.open('rb') as stream:
            actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if actual != checksum:
            raise ValueError('Unsupported stock trial checksum: ' + relative)
        result.append({'source': str(path), 'path': 'x6/stock/' + target,
                       'size': size, 'sha256': actual})
    return result


def stage(backup, staging):
    rows = inspect(backup)  # Validate every input before creating output.
    staging = Path(staging).resolve()
    source_root = Path(backup).expanduser().resolve()
    if staging == source_root or source_root in staging.parents or staging in source_root.parents:
        raise ValueError('Stock output must be separate from the source backup')
    metadata = stock_metadata.extract(source_root / 'apps/mod/fba.mod')
    title_file = staging / 'x6/stock/arcade-titles.tsv'
    if title_file.exists() or title_file.is_symlink():
        raise ValueError('Stock title output already exists')
    for folder, extensions in ROM_FOLDERS.items():
        directory = source_root / 'game' / folder
        if directory.is_symlink():
            raise ValueError('Stock ROM folders must not be symlinks')
        if not directory.is_dir():
            continue
        for path in sorted(directory.iterdir()):
            if path.name.startswith('.') or path.suffix.lower() not in extensions:
                continue
            if path.is_symlink() or not stat.S_ISREG(path.stat().st_mode):
                raise ValueError('Stock ROM input must be a regular file')
            if folder == 'mame' and path.name == '2020bb.fba':
                continue  # The validated seed already lives in x6/stock.
            with path.open('rb') as stream:
                checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
            rows.append({'source': str(path), 'path': 'game/' + folder + '/' + path.name,
                         'size': path.stat().st_size, 'sha256': checksum})
    for row in rows:
        target = staging / row['path']
        if any(p.is_symlink() for p in [target, *target.parents]):
            raise ValueError('Stock output must not contain symlinks')
        if target.exists():
            raise ValueError('Stock output already exists: ' + row['path'])
    for row in rows:
        target = staging / row['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(row['source'], target)
        with target.open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != row['sha256']:
                raise ValueError('Stock output readback differs: ' + row['path'])
    title_file.write_text(metadata, encoding='ascii')
    return {'kind': 'original arcade and console module compatibility trial',
            'title_records': len(metadata.splitlines()) - 1,
            'hardware_tested': False, 'files': rows, 'saves_imported': False}
