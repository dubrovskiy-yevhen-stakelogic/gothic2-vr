"""Offline Quest installer for the Windows 7 SP1 x86 compatibility package."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
APP = 'com.gothic2vr.quest'
FILES = '/sdcard/Android/data/' + APP + '/files'
RESERVE = 512 * 1024 * 1024


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def verify_tools(root):
    lock = read_json(root / 'config/win7-tools.lock.json')
    for entry in lock['files']:
        rel = Path(entry['path'])
        if rel.is_absolute() or '..' in rel.parts or ':' in str(rel):
            raise RuntimeError('Invalid compatibility tool path')
        if sha(root / rel) != entry['sha256']:
            raise RuntimeError('Compatibility tool checksum mismatch: ' + str(rel))


class Adb:
    def __init__(self, root):
        self.exe = root / 'legacy/adb/adb.exe'
        self.serial = None

    def run(self, *args, allowed=(0,)):
        # Keep this older server separate from SideQuest and modern SDK servers.
        command = [str(self.exe), '-P', '5038']
        if self.serial:
            command += ['-s', self.serial]
        env = os.environ.copy()
        for name in ['ADB_SERVER_SOCKET', 'ANDROID_ADB_SERVER_PORT', 'ADB_TRACE']:
            env.pop(name, None)
        result = subprocess.run(command + list(args), stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, env=env)
        text = result.stdout.decode('utf-8', errors='replace').strip()
        if result.returncode not in allowed:
            error = result.stderr.decode('utf-8', errors='replace').strip()
            raise RuntimeError('ADB failed ({}): {} {}'.format(result.returncode, text, error))
        return result.returncode, text

    def select(self, serial=None):
        _, text = self.run('devices')
        online = re.findall(r'^([A-Za-z0-9._:-]+)\s+device\s*$', text, re.M)
        if not serial and len(online) == 1:
            serial = online[0]
        if serial not in online:
            raise RuntimeError('Connect one Quest, enable Developer Mode and accept USB debugging. '
                               'If it is absent, check the Quest USB driver. Device list:\n' + text)
        self.serial = serial
        _, model = self.run('shell', 'getprop', 'ro.product.model')
        if not model.startswith('Quest'):
            raise RuntimeError('Selected device is not a Quest: ' + model)
        return model

    def exists(self, path, kind='-f'):
        code, _ = self.run('shell', 'test', kind, path, allowed=(0, 1))
        return code == 0

    def digest(self, path):
        _, text = self.run('shell', 'sha256sum', path)
        match = re.match(r'^([0-9a-fA-F]{64})\s', text)
        if not match:
            raise RuntimeError('Cannot read device checksum: ' + path)
        return match[1].lower()

    def saved_state(self):
        if not self.exists(FILES, '-d'):
            return {}
        _, listing = self.run('shell', 'ls', '-1', FILES)
        names = [x for x in listing.splitlines() if re.fullmatch(r'[A-Za-z0-9_.-]+\.(?:sav|ini)', x, re.I)]
        return {name: self.digest(FILES + '/' + name) for name in sorted(names)}


def verify_apk(root):
    manifest = read_json(root / 'release.json')
    name = manifest['apk']['file']
    if manifest['applicationId'] != APP or not re.fullmatch(r'[A-Za-z0-9_.-]+\.apk', name):
        raise RuntimeError('Invalid APK release manifest')
    apk = root / name
    digest = sha(apk)
    if digest != manifest['apk']['sha256'].lower():
        raise RuntimeError('APK checksum mismatch; extract a fresh compatibility package')
    return apk, digest


def prepare_data(root, game=None):
    manifest = root / 'build/private/game-data.manifest.json'
    if manifest.exists():
        if game:
            raise RuntimeError('A prepared archive already exists. Use another extracted installer folder for another game installation.')
    else:
        if game is None:
            game = input('Purchased Gothic II Gold / NotR folder (contains Data, _work, System): ').strip().strip('"')
        subprocess.run([sys.executable, '-B', str(root / 'tools/package-game-data.py'), '--game-root', game], check=True)
    subprocess.run([sys.executable, '-B', str(root / 'tools/verify-game-data.py')], check=True)
    data = read_json(manifest)
    archive = root / 'build/private/game-data.zip'
    if Path(data['output']).resolve() != archive.resolve() or sha(archive) != data['sha256']:
        raise RuntimeError('Private archive path/checksum mismatch')
    return archive, data


def install(root, adb, apk, digest, data, report, dry_run=False):
    before = adb.saved_state()
    report['before'] = before
    _, space = adb.run('shell', 'df', '-k', '/sdcard')
    available = int(space.splitlines()[-1].split()[3]) * 1024
    required = RESERVE + (data[0].stat().st_size + int(data[1]['bytes']) if data else 0)
    if available < required:
        raise RuntimeError('Not enough free headset storage for APK, import and 512 MiB reserve')
    if dry_run:
        report['status'] = 'dry-run-passed'
        return
    _, text = adb.run('install', '--no-streaming', '-r', str(apk))
    if not re.search(r'^Success\s*$', text, re.M):
        raise RuntimeError('APK installation did not report Success. Keep the existing app and its data.')
    report['installed'] = True
    _, path = adb.run('shell', 'pm', 'path', APP)
    match = re.fullmatch(r'package:(/[A-Za-z0-9_./=+~-]+)', path)
    if not match or adb.digest(match[1]) != digest:
        raise RuntimeError('Installed APK checksum differs from the release')
    after = adb.saved_state()
    report['after'] = after
    report['preserved'] = all(after.get(name) == value for name, value in before.items())
    if not report['preserved']:
        raise RuntimeError('A save/settings checksum changed; inspect the local deployment report')
    if data:
        archive, manifest = data
        remote = '/sdcard/Download/Gothic2VR/game-data-' + manifest['sha256'][:12] + '.zip'
        adb.run('shell', 'mkdir', '-p', '/sdcard/Download/Gothic2VR')
        if adb.exists(remote):
            if adb.digest(remote) != manifest['sha256']:
                raise RuntimeError('Existing destination differs; it was not replaced')
        else:
            partial = remote + '.part'
            if adb.exists(partial, '-e'):
                raise RuntimeError('Previous partial transfer preserved: ' + partial)
            adb.run('push', str(archive), partial)
            if adb.digest(partial) != manifest['sha256']:
                raise RuntimeError('Transferred ZIP checksum mismatch; partial file retained')
            adb.run('shell', 'mv', partial, remote)
        report['dataTransferred'] = True
        _, result = adb.run('shell', 'am', 'start', '-W', '-a', APP + '.IMPORT_GAME_FILES',
                            '-n', APP + '/org.opengothic.app.SetupActivity')
        if 'Status: ok' not in result:
            raise RuntimeError('ZIP transferred, but importer did not open: ' + result)
        report['importerOpened'] = True
        print('In the headset select the ZIP in Download/Gothic2VR and wait for import.')
    report['status'] = 'passed'
    print('APK installed and verified. Saves/settings preserved. The game was not launched.')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--package-only', action='store_true')
    modes.add_argument('--update-only', action='store_true')
    modes.add_argument('--check-only', action='store_true')
    modes.add_argument('--import-only', action='store_true')
    parser.add_argument('--game-root')
    parser.add_argument('--serial')
    args = parser.parse_args(argv)
    report = {'installed': False, 'dataTransferred': False, 'importerOpened': False,
              'gameLaunched': False, 'status': 'failed', 'host': sys.getwindowsversion().major if os.name == 'nt' else None}
    logs = ROOT / 'build/win7-reports'
    logs.mkdir(parents=True, exist_ok=True)
    try:
        verify_tools(ROOT)
        if args.package_only:
            archive, _ = prepare_data(ROOT, args.game_root)
            print('Copy this ZIP to /sdcard/Download/Gothic2VR/: ' + str(archive))
            report['status'] = 'package-passed'
            return 0
        apk, digest = verify_apk(ROOT)
        adb = Adb(ROOT)
        report['model'] = adb.select(args.serial)
        report['serial'] = adb.serial
        report['apkSha256'] = digest
        if args.import_only:
            _, result = adb.run('shell', 'am', 'start', '-W', '-a', APP + '.IMPORT_GAME_FILES',
                                '-n', APP + '/org.opengothic.app.SetupActivity')
            if 'Status: ok' not in result:
                raise RuntimeError('Could not open the import screen: ' + result)
            report['importerOpened'] = True
            report['status'] = 'passed'
            return 0
        data = None
        if not args.update_only and not args.check_only:
            if args.game_root or not adb.exists(FILES + '/Gothic2/Data', '-d'):
                data = prepare_data(ROOT, args.game_root)
        install(ROOT, adb, apk, digest, data, report, dry_run=args.check_only)
        return 0
    except (Exception, KeyboardInterrupt) as error:
        report['error'] = str(error)
        print('Installation stopped: ' + str(error), file=sys.stderr)
        return 1
    finally:
        stamp = datetime.datetime.utcnow().strftime('%Y%m%d-%H%M%S-%f')
        (logs / (stamp + '.json')).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print('Diagnostic reports: ' + str(logs))


if __name__ == '__main__':
    sys.exit(main())
