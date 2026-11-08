"""Exercise the compatibility installer without contacting a physical device."""
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('legacy', ROOT / 'tools/install-win7.py')
legacy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(legacy)


class FakeAdb(legacy.Adb):
    def __init__(self, scenario='ok'):
        self.calls = []
        self.scenario = scenario
        self.installed = False

    def run(self, *args, allowed=(0,)):
        self.calls.append(args)
        if args[:3] == ('shell', 'df', '-k'):
            return 0, 'Filesystem 1K-blocks Used Available Use% Mounted on\nmock 999999999 0 {} 0% /sdcard'.format(1 if self.scenario == 'space' else 999999999)
        if args[0] == 'install':
            if self.scenario == 'signature':
                raise RuntimeError('INSTALL_FAILED_UPDATE_INCOMPATIBLE')
            self.installed = True
            return 0, 'Success'
        if args[:3] == ('shell', 'pm', 'path'):
            return 0, 'package:/data/app/~~test==/com.gothic2vr.quest-test==/base.apk'
        if args[:3] == ('shell', 'am', 'start'):
            return 0, 'Status: ok'
        if args[0] == 'push' or args[:2] in [('shell', 'mkdir'), ('shell', 'mv')]:
            return 0, ''
        raise AssertionError(args)

    def saved_state(self):
        return {'VR.ini': 'changed' if self.installed and self.scenario == 'save' else 'original', 'save_slot_1.sav': 'save'}

    def digest(self, path):
        if path.endswith('base.apk'):
            return 'wrong' if self.scenario == 'apk-hash' else 'a' * 64
        return 'wrong' if self.scenario in ['transfer', 'destination'] else 'b' * 64

    def exists(self, path, kind='-f'):
        return (path.endswith('.part') and self.scenario == 'partial') or (path.endswith('.zip') and self.scenario in ['destination', 'reuse'])


class Installer(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='gothic-win7-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.apk = self.root / 'app.apk';self.apk.write_bytes(b'fixture')
        self.zip = self.root / 'game.zip';self.zip.write_bytes(b'fixture')
        self.data = (self.zip, {'bytes': 25, 'sha256': 'b' * 64})

    def test_update_preserves_state(self):
        adb = FakeAdb();report = {}
        legacy.install(self.root, adb, self.apk, 'a' * 64, None, report)
        self.assertTrue(report['preserved'])
        self.assertFalse(any(c[0] == 'push' or 'am' in c for c in adb.calls))
        self.assertIn(('install', '--no-streaming', '-r', str(self.apk)), adb.calls)

    def test_dry_run_no_device_writes(self):
        adb = FakeAdb();report = {}
        legacy.install(self.root, adb, self.apk, 'a' * 64, None, report, dry_run=True)
        self.assertFalse(adb.installed)
        self.assertEqual(report['status'], 'dry-run-passed')

    def test_storage_and_signature_failures(self):
        for scenario in ['space', 'signature']:
            adb = FakeAdb(scenario)
            with self.assertRaises(RuntimeError):legacy.install(self.root, adb, self.apk, 'a' * 64, self.data, {})
            self.assertFalse(adb.installed)
            self.assertFalse(any('uninstall' in c or c[0] == 'push' for c in adb.calls))

    def test_bad_install_or_changed_saves_stops_transfer(self):
        for scenario in ['save', 'apk-hash']:
            adb = FakeAdb(scenario)
            with self.assertRaises(RuntimeError):legacy.install(self.root, adb, self.apk, 'a' * 64, self.data, {})
            self.assertFalse(any(c[0] == 'push' for c in adb.calls))

    def test_partial_and_existing_destination_preserved(self):
        for scenario in ['partial', 'destination']:
            adb = FakeAdb(scenario)
            with self.assertRaises(RuntimeError):legacy.install(self.root, adb, self.apk, 'a' * 64, self.data, {})
            self.assertFalse(any(c[0] == 'push' or c[:2] == ('shell', 'mv') for c in adb.calls))

    def test_corrupt_transfer_not_committed(self):
        adb = FakeAdb('transfer')
        with self.assertRaises(RuntimeError):legacy.install(self.root, adb, self.apk, 'a' * 64, self.data, {})
        self.assertTrue(any(c[0] == 'push' for c in adb.calls))
        self.assertFalse(any(c[:2] == ('shell', 'mv') or 'am' in c for c in adb.calls))

    def test_transfer_and_reuse(self):
        for scenario in ['ok', 'reuse']:
            adb = FakeAdb(scenario);report = {}
            legacy.install(self.root, adb, self.apk, 'a' * 64, self.data, report)
            self.assertTrue(report['dataTransferred'] and report['importerOpened'])
            self.assertEqual(any(c[0] == 'push' for c in adb.calls), scenario == 'ok')

    def test_tampered_apk(self):
        (self.root / 'release.json').write_text(json.dumps({'applicationId':legacy.APP,'apk':{'file':'app.apk','sha256':legacy.sha(self.apk)}}))
        legacy.verify_apk(self.root)
        self.apk.write_bytes(b'changed')
        with self.assertRaises(RuntimeError):legacy.verify_apk(self.root)

    def test_tool_checksum_failure(self):
        (self.root/'config').mkdir()
        lock=self.root/'config/win7-tools.lock.json'
        lock.write_text(json.dumps({'files':[{'path':'app.apk','sha256':legacy.sha(self.apk)}]}))
        legacy.verify_tools(self.root)
        self.apk.write_bytes(b'changed')
        with self.assertRaises(RuntimeError):legacy.verify_tools(self.root)

    def test_device_selection(self):
        adb=legacy.Adb(self.root)
        replies=['List of devices attached\nFIRST\tunauthorized', 'List of devices attached\nFIRST\tdevice\nSECOND\tdevice']
        for reply in replies:
            adb.run=lambda *args,**kwargs:(0,reply)
            with self.assertRaises(RuntimeError):adb.select()
        adb.run=lambda *args,**kwargs:(0,'List of devices attached\nFIRST\tdevice' if args==('devices',) else 'Quest 3')
        self.assertEqual(adb.select(),'Quest 3')
        self.assertEqual(adb.serial,'FIRST')

    def test_package_and_verify_with_this_python(self):
        for name in ['tools/package-game-data.py','tools/verify-game-data.py','engine/android/tools/game_package.py']:
            dest=self.root/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,dest)
        game=self.root/'Purchased game';(game/'Data').mkdir(parents=True);(game/'_work').mkdir()
        header=bytearray(296);header[256:272]=b'PSVDSC_V2.00\r\n\r\n';struct.pack_into('<6I',header,272,2,0,0,0,296,0)
        entries=[]
        for name in [b'NEWWORLD.ZEN',b'ADDONWORLD.ZEN']:
            entry=bytearray(80);entry[:len(name)]=name;entries.append(entry)
        (game/'Data/Worlds.vdf').write_bytes(header+b''.join(entries))
        (game/'_work/ignore.dll').write_bytes(b'excluded')
        (game/'_work/asset.txt').write_bytes(b'asset')
        archive,manifest=legacy.prepare_data(self.root,str(game))
        self.assertEqual(manifest['entries'],2)
        self.assertTrue(archive.is_file())
        legacy.prepare_data(self.root)
        with self.assertRaises(RuntimeError):legacy.prepare_data(self.root,str(game))
        archive.write_bytes(b'corrupt')
        with self.assertRaises(Exception):legacy.prepare_data(self.root)

    def test_package_only_never_uses_adb(self):
        oldroot,oldverify,oldprepare,oldadb=legacy.ROOT,legacy.verify_tools,legacy.prepare_data,legacy.Adb
        try:
            legacy.ROOT=self.root;legacy.verify_tools=lambda root:None
            legacy.prepare_data=lambda root,game:(self.zip,{})
            legacy.Adb=lambda root:self.fail('Package-only contacted ADB')
            self.assertEqual(legacy.main(['--package-only']),0)
        finally:legacy.ROOT,legacy.verify_tools,legacy.prepare_data,legacy.Adb=oldroot,oldverify,oldprepare,oldadb


if __name__ == '__main__':
    print('Interpreter:',sys.version, 'pointer bits:',struct.calcsize('P')*8)
    unittest.main()
