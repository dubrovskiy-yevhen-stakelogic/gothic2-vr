"""Build the offline Win7 compatibility player package from a verified Quest release."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-player',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--download',action='store_true')
    args=parser.parse_args()
    manifest=json.loads((args.base_player/'release.json').read_text(encoding='utf-8-sig'))
    name=manifest['apk']['file']
    if manifest['applicationId']!='com.gothic2vr.quest' or not re.fullmatch(r'[A-Za-z0-9_.-]+\.apk',name):raise ValueError('Invalid player manifest')
    if sha(args.base_player/name)!=manifest['apk']['sha256'].lower():raise ValueError('APK checksum mismatch')
    if args.output.exists():raise ValueError('Output already exists; preserved')
    lock=json.loads((ROOT/'config/win7-tools.lock.json').read_text())
    cache=ROOT/'build/win7-downloads';cache.mkdir(parents=True,exist_ok=True)
    archives=[]
    for entry in lock['archives']:
        archive=cache/(entry['id']+'.zip')
        if not archive.exists():
            if not args.download:raise ValueError('Cached tool missing: '+str(archive))
            urllib.request.urlretrieve(entry['url'],archive)
        if sha(archive)!=entry['sha256']:raise ValueError('Downloaded tool checksum mismatch: '+entry['id'])
        archives.append((entry,archive))
    version=manifest['version']
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+',version):raise ValueError('Invalid release version')
    folder=args.output/('Gothic-II-VR-'+version+'-Quest-Win7-x86');folder.mkdir(parents=True)
    for p in args.base_player.rglob('*'):
        if not p.is_file():continue
        rel=p.relative_to(args.base_player)
        if p.suffix.lower() in {'.ini','.sav','.vdf','.dat','.zen','.keystore','.jks','.log'} or any(x in {'build','toolchain','.git','logs'} for x in rel.parts):raise ValueError('Base player is not a clean release: '+str(rel))
        target=folder/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
    names=['INSTALL-WIN7.bat','PACKAGE-GAME-DATA-WIN7.bat','UPDATE-WIN7.bat','WINDOWS7.md',
           'tools/install-win7.py','tools/run-win7.cmd','tools/package-game-data.py',
           'tools/verify-game-data.py','engine/android/tools/game_package.py','config/win7-tools.lock.json']
    for name in names:
        target=folder/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,target)
    for entry,archive in archives:
        with zipfile.ZipFile(archive) as z:
            for member in z.infolist():
                if member.is_dir():continue
                rel=Path(member.filename)
                if rel.is_absolute() or '..' in rel.parts or ':' in str(rel):raise ValueError('Unsafe tool archive path')
                if entry['id']=='adb':
                    if member.filename not in entry['include']:continue
                    rel=Path(rel.name)
                target=folder/entry['destination']/rel
                target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(member))
    for entry in lock['files']:
        if sha(folder/entry['path'])!=entry['sha256']:raise ValueError('Extracted tool checksum mismatch')
    aliases={'INSTALL.bat':'','UPDATE.bat':'--update-only','PACKAGE-GAME-DATA.bat':'--package-only','OPEN-DATA-IMPORT.bat':'--import-only'}
    for name,mode in aliases.items():
        (folder/name).write_bytes(('@echo off\r\ncall "%~dp0tools\\run-win7.cmd" '+mode+' %*\r\nexit /b %errorlevel%\r\n').encode('ascii'))
    (folder/'README.md').write_text('# Gothic II VR '+version+' - Quest installer for Windows 7\n\nStart with **INSTALL-WIN7.bat**. See [WINDOWS7.md](WINDOWS7.md) for installation, ZIP-only packaging and troubleshooting. This package is for standalone Quest, not PCVR. Windows 7 runtime acceptance is pending.\n',encoding='utf-8')
    shutil.copy2(ROOT/'WINDOWS7.md',folder/'INSTALL.md')
    (folder/'FILES-SHA256.txt').write_text(''.join(sha(p)+'  '+p.relative_to(folder).as_posix()+'\n' for p in sorted(folder.rglob('*')) if p.is_file() and p.name!='FILES-SHA256.txt'),encoding='utf-8')
    output=args.output/(folder.name+'.zip')
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(folder.rglob('*')):
            if p.is_file():z.write(p,p.relative_to(folder).as_posix())
    with zipfile.ZipFile(output) as z:
        if z.testzip() is not None:raise ValueError('Player ZIP integrity check failed')
    (args.output/'SHA256SUMS.txt').write_text(sha(output)+'  '+output.name+'\n',encoding='ascii')
    print(output)
    print('SHA256: '+sha(output))


if __name__=='__main__':main()
