"""Create and audit a clean source archive from the working checkout."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TREES = ('android', 'config', 'engine', 'licenses', 'pcvr', 'tests', 'tools')
DOCUMENTS = ('README.md', 'INSTALL.md', 'BUILDING.md', 'CONTROLS.md', 'CHANGELOG.md',
             'RELEASE-NOTES.md', 'KNOWN_ISSUES.md', 'RELEASE_REVIEW.md', 'LICENSE',
             'NOTICE.md', 'THIRD_PARTY_NOTICES.md', 'TECHNICAL-OVERVIEW.md', 'WINDOWS7.md',
             '.gitignore', '.gitattributes')
SKIP = {'.git', '__pycache__', '.cache'}

def run(*args):
    subprocess.run([sys.executable, '-B', *map(str, args)], check=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    stage = output / 'Source'
    extracted = output / 'Source-verified'
    if stage.exists() or extracted.exists():
        raise RuntimeError('Use a fresh output directory; existing snapshots are not overwritten.')
    stage.mkdir()
    files = [ROOT / name for name in DOCUMENTS] + list(ROOT.glob('*.bat'))
    for name in TREES:
        files.extend(p for p in (ROOT / name).rglob('*') if p.is_file()
                     and not any(part in SKIP for part in p.relative_to(ROOT).parts)
                     and p.suffix != '.pyc')
    for source in sorted(files):
        if source.is_symlink():
            raise RuntimeError(f'Linked input: {source}')
        dest = stage / source.relative_to(ROOT)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)
    run(stage / 'tools/audit-source-kit.py', '--root', stage, '--write-manifest')
    run(stage / 'tools/audit-source-kit.py', '--root', stage)
    import json
    version = json.loads((stage / 'SOURCE-MANIFEST.json').read_text())['version']
    archive = output / f'Gothic-II-VR-{version}-Source.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in sorted(stage.rglob('*')):
            if p.is_file():
                z.write(p, p.relative_to(stage).as_posix())
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None:
            raise RuntimeError('Archive CRC check failed.')
        z.extractall(extracted)
    run(extracted / 'tools/audit-source-kit.py', '--root', extracted)
    # The checkout inventory describes the distributable snapshot, not local caches.
    for name in ('SOURCE-MANIFEST.json', 'SOURCE-SHA256.txt'):
        shutil.copy2(stage / name, ROOT / name)
    print(f'{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}')

if __name__ == '__main__':
    main()
