"""Check the publishable source tree; private folders are deliberately excluded."""
import ast
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
allowed = ['src', 'include', 'boot', 'tools', 'tests', 'config', 'assets', '.github']
if (root / '.git').is_dir():
    tracked = subprocess.check_output(['git', 'ls-files', '-z'], cwd=root).decode().split('\0')
    root_files = {'README.md', 'SETUP.md', 'BACKUP.md', 'WINDOWS.md', 'DEPENDENCIES.md', 'LICENSE', 'Makefile', '.gitignore'}
    for name in filter(None, tracked):
        path = Path(name)
        if name not in root_files and path.parts[0] not in allowed:
            raise SystemExit('Unapproved public path: ' + name)
        if path.name.upper() in {'AGENTS.MD', 'AGENT.MD', 'CLAUDE.MD'}:
            raise SystemExit('Agent instruction file staged for publication: ' + name)
for directory in allowed:
    for path in (root / directory).rglob('*'):
        if not path.is_file() or '__pycache__' in path.parts:
            continue
        if path.is_symlink() or path.suffix.lower() in {'.zip', '.bin', '.img', '.iso', '.mod', '.axf', '.elf', '.o', '.a', '.drv'}:
            raise SystemExit('Private/generated file in public source: ' + str(path))
        if path.name.upper() in {'AGENTS.MD', 'AGENT.MD', 'CLAUDE.MD'}:
            raise SystemExit('Agent instruction file in public source: ' + str(path))
        if path.suffix == '.py':
            ast.parse(path.read_text(), filename=str(path))
        elif path.suffix == '.json':
            json.loads(path.read_text())
        if b'/' + b'Users/' in path.read_bytes() or b'/' + b'private/tmp/' in path.read_bytes():
            raise SystemExit('Machine-specific path in public source: ' + str(path))
subprocess.run([sys.executable, str(root / 'tools/gen_font.py'), '--check'], check=True)
print('Source checks passed; no bundled ROMs, firmware binaries, agent files, or machine-specific paths.')
