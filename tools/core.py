"""Prepare a private pinned core checkout using original X6 edits only."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / 'local/mame2000'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def prepare(source=None):
    spec = json.loads((ROOT / 'config/core-edits.json').read_text())
    profile = json.loads((ROOT / 'config/mame2000-arcade.json').read_text())
    if not UPSTREAM.exists():
        UPSTREAM.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', 'clone', '--no-hardlinks', '--no-checkout',
                        str(source or profile['upstream']), str(UPSTREAM)], check=True)
        subprocess.run(['git', 'checkout', '--detach', spec['commit']], cwd=UPSTREAM, check=True)
    actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=UPSTREAM, text=True).strip()
    if actual != spec['commit']:
        raise ValueError('Core revision differs. Use a fresh local/mame2000 checkout.')
    for item in spec['files']:
        path = UPSTREAM / item['path']
        original = path.read_bytes()
        if digest(original) == item['result_sha256']:
            continue
        if digest(original) != item['sha256']:
            raise ValueError('Refusing to patch changed source: ' + item['path'])
        lines = original.splitlines(keepends=True)
        for edit in reversed(item['edits']):
            start, count = edit['line'], edit['remove']
            if digest(b''.join(lines[start:start + count])) != edit['before_sha256']:
                raise ValueError('Core edit preflight failed: ' + item['path'])
            lines[start:start + count] = edit['insert'].encode().splitlines(keepends=True)
        result = b''.join(lines)
        if digest(result) != item['result_sha256']:
            raise ValueError('Core edit result differs: ' + item['path'])
        path.write_bytes(result)
    verify()
    return UPSTREAM


def verify():
    spec = json.loads((ROOT / 'config/core-edits.json').read_text())
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=UPSTREAM, text=True).strip()
    if revision != spec['commit']:
        raise ValueError('Wrong core revision')
    for item in spec['files']:
        if digest((UPSTREAM / item['path']).read_bytes()) != item['result_sha256']:
            raise ValueError('Core source changed: ' + item['path'])
    # Check every other tracked file, as well as staged changes.
    changed = set(subprocess.check_output(['git', 'diff', 'HEAD', '--name-only'], cwd=UPSTREAM, text=True).splitlines())
    if changed != {item['path'] for item in spec['files']}:
        raise ValueError('Unexpected core modifications')
