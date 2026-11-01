"""Import the first original-FBA trial from the user's local stock-card backup.

Only the exact inspected module and 2020bb packed image are accepted for this
experiment. No downloads, save-file import, device I/O, or input modifications.
"""
import hashlib
from pathlib import Path
import shutil
import stat

FILES = {
    'apps/mod/fba.mod': ('fba.mod', 2259132, '98d5b12acbef39a0b3b9ec4c367c7cebd8a975b4391472fa763090d4aca6b759'),
    'game/mame/2020bb.fba': ('2020bb.fba', 8889600, '07a39676091dea99883339e61656bb3fa4c53a092af64217490f394fac7b45d1'),
}


def inspect(backup):
    backup = Path(backup).expanduser().resolve(strict=True)
    if not backup.is_dir():
        raise ValueError('STOCK_BACKUP must be a local stock-card backup directory')
    result = []
    for relative, (target, size, checksum) in FILES.items():
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
    return {'kind': 'original FBA / 2020 Super Baseball compatibility trial',
            'hardware_tested': False, 'files': rows, 'saves_imported': False}
