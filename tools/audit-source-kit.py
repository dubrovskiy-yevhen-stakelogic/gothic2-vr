"""Check the source distribution for missing files and non-distributable payloads."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import stat
import sys

FORBIDDEN_DIRS = {'.git', '.svn', '.cache', '__pycache__', 'build', 'toolchain',
                  'runtime', 'logs', 'research', 'upstream', 'saves', 'savegames'}
FORBIDDEN_EXTENSIONS = {
    '.vdf', '.mod', '.zen', '.dat', '.sav', '.zsl', '.mrm', '.mds', '.mdm',
    '.mdh', '.mdl', '.man', '.msb', '.mmb', '.3ds', '.asc', '.tex', '.tga',
    '.bik', '.wav', '.ogg', '.mp3', '.mp4', '.exe', '.dll', '.so', '.apk',
    '.aab', '.o', '.obj', '.lib', '.a', '.pdb', '.zip', '.7z', '.rar',
    '.jks', '.keystore', '.p12', '.pfx', '.key', '.pem', '.pyc',
}
META = {'SOURCE-MANIFEST.json', 'SOURCE-SHA256.txt'}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def check(root, allow_local_state=False):
    errors, records = [], []
    assets = json.loads((root / 'config/distributable-assets.json').read_text(encoding='utf-8-sig'))
    allowed_binary = {f['path']: f['sha256'] for f in assets['files']}
    for path in sorted(root.rglob('*')):
        rel = path.relative_to(root).as_posix()
        if rel == '.git' or rel.startswith('.git/'):
            continue
        if allow_local_state and rel.split('/')[0] in {'build', 'toolchain', 'logs'}:
            continue
        info = path.lstat()
        if path.is_symlink() or getattr(info, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            errors.append(f'Linked path: {rel}')
            continue
        if not path.is_file():
            if path.name.lower() in FORBIDDEN_DIRS:
                errors.append(f'Local-only directory: {rel}')
            continue
        if path.suffix.lower() in FORBIDDEN_EXTENSIONS:
            errors.append(f'Forbidden payload extension: {rel}')
        if re.search(r'(?:^|/)(?:gothic|gamepad|vr)\.ini$|\.vdf\.', rel, re.I):
            errors.append(f'Game data or personal settings: {rel}')
        data = path.read_bytes()
        digest = sha(data)
        if rel in allowed_binary:
            if digest != allowed_binary[rel]:
                errors.append(f'Licensed binary resource hash changed: {rel}')
        elif b'\x00' in data or data.startswith((b'MZ', b'PK\x03\x04', b'\x7fELF')):
            errors.append(f'Unlisted binary: {rel}')
        if re.search(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----', data):
            errors.append(f'Private key material: {rel}')
        if rel not in META:
            records.append({'path': rel, 'sha256': digest, 'bytes': len(data)})
    actual = {r['path'] for r in records}
    for rel in allowed_binary:
        if rel not in actual:
            errors.append(f'Missing licensed resource: {rel}')
    # Check user-facing local links; historical upstream docs keep their own context.
    for path in sorted(root.glob('*.md')):
        for target in re.findall(r'\]\(([^\s)]+)(?:\s+"[^"]*")?\)', path.read_text(encoding='utf-8-sig')):
            if '://' in target or target.startswith('#'):
                continue
            target = target.split('#', 1)[0]
            if target and target not in META and not (root / target).exists():
                errors.append(f'Broken documentation link in {path.name}: {target}')
    return records, errors

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--write-manifest', action='store_true', help='Maintainers: record a reviewed clean snapshot.')
    parser.add_argument('--allow-local-state', action='store_true', help='Ignore local build, toolchain and logs directories; never use for archive validation.')
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        records, errors = check(root, args.allow_local_state)
        cmake = (root / 'android/CMakeLists.txt').read_text(encoding='utf-8')
        version = re.search(r'VERSION_NAME\s+([^\s)]+)', cmake).group(1)
        manifest = root / 'SOURCE-MANIFEST.json'
        if args.write_manifest and not errors:
            manifest.write_text(json.dumps({'schemaVersion': 1, 'version': version,
                'files': records}, indent=2) + '\n', encoding='utf-8')
            all_hashes = records + [{'path': manifest.name, 'sha256': sha(manifest.read_bytes())}]
            (root / 'SOURCE-SHA256.txt').write_text(''.join(
                f"{r['sha256']}  {r['path']}\n" for r in sorted(all_hashes, key=lambda r: r['path'])), encoding='utf-8')
        elif not args.write_manifest:
            expected = json.loads(manifest.read_text(encoding='utf-8'))
            if expected['version'] != version:
                errors.append('Version differs from source manifest.')
            old = {r['path']: (r['sha256'], r['bytes']) for r in expected['files']}
            new = {r['path']: (r['sha256'], r['bytes']) for r in records}
            errors += [f'Missing file: {p}' for p in old.keys() - new.keys()]
            errors += [f'Unexpected file: {p}' for p in new.keys() - old.keys()]
            errors += [f'Changed file: {p}' for p in old.keys() & new.keys() if old[p] != new[p]]
            expected_lines = ''.join(f"{r['sha256']}  {r['path']}\n" for r in sorted(
                records + [{'path': manifest.name, 'sha256': sha(manifest.read_bytes())}], key=lambda r: r['path']))
            if (root / 'SOURCE-SHA256.txt').read_text(encoding='utf-8') != expected_lines:
                errors.append('SOURCE-SHA256.txt differs from the file manifest.')
    except (OSError, ValueError, KeyError, AttributeError) as error:
        errors = [str(error)]
        records = []
    if errors:
        print('\n'.join(sorted(errors)), file=sys.stderr)
        return 1
    print(f"PASS: {len(records)} source/resource files; no game payloads, keys or build outputs; hashes verified.")
    return 0

if __name__ == '__main__':
    sys.exit(main())
