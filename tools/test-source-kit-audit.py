"""Exercise publication guards using synthetic temporary files."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
AUDITOR = Path(__file__).with_name('audit-source-kit.py')
spec = importlib.util.spec_from_file_location('source_audit', AUDITOR)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)

class PublicationGuards(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='gothic-source-audit-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'config').mkdir()
        (self.root / 'config/distributable-assets.json').write_text('{"files":[]}', encoding='utf-8')
        (self.root / 'android').mkdir()
        (self.root / 'android/CMakeLists.txt').write_text('VERSION_NAME 0.0.48-batch1\n', encoding='utf-8')

    def run_audit(self, *args):
        return subprocess.run([sys.executable, '-B', str(AUDITOR), '--root', str(self.root), *args],
                              capture_output=True, text=True)

    def test_clean_manifest_and_readback(self):
        self.assertEqual(self.run_audit('--write-manifest').returncode, 0)
        self.assertEqual(self.run_audit().returncode, 0)

    def test_retail_extension_rejected(self):
        (self.root / 'synthetic.vdf').write_bytes(b'not real game data')
        self.assertIn('Forbidden payload extension', '\n'.join(audit.check(self.root)[1]))

    def test_renamed_binary_rejected(self):
        for payload in (b'MZsynthetic', b'PK\x03\x04synthetic', b'abc\x00def'):
            with self.subTest(payload=payload):
                (self.root / 'innocent.txt').write_bytes(payload)
                self.assertIn('Unlisted binary', '\n'.join(audit.check(self.root)[1]))

    def test_build_directory_rejected(self):
        (self.root / 'build').mkdir()
        self.assertIn('Local-only directory', '\n'.join(audit.check(self.root)[1]))

    def test_changed_or_extra_source_rejected(self):
        self.assertEqual(self.run_audit('--write-manifest').returncode, 0)
        p = self.root / 'android/CMakeLists.txt'
        original = p.read_bytes()
        p.write_bytes(original + b'# change\n')
        self.assertIn('Changed file:', self.run_audit().stderr)
        p.write_bytes(original)
        (self.root / 'extra.txt').write_text('extra', encoding='utf-8')
        self.assertIn('Unexpected file:', self.run_audit().stderr)

    def test_licensed_resource_tamper_rejected(self):
        p = self.root / 'font.ttf'
        p.write_bytes(b'\x00fixture')
        (self.root / 'config/distributable-assets.json').write_text(json.dumps({'files': [
            {'path': 'font.ttf', 'sha256': audit.sha(p.read_bytes())}]}), encoding='utf-8')
        self.assertFalse(audit.check(self.root)[1])
        p.write_bytes(b'\x00tampered')
        self.assertIn('resource hash changed', '\n'.join(audit.check(self.root)[1]))

    def test_missing_source_and_hash_list_rejected(self):
        self.assertEqual(self.run_audit('--write-manifest').returncode, 0)
        (self.root / 'SOURCE-SHA256.txt').write_text('wrong\n', encoding='utf-8')
        self.assertIn('SOURCE-SHA256.txt differs', self.run_audit().stderr)
        (self.root / 'android/CMakeLists.txt').unlink()
        self.assertNotEqual(self.run_audit().returncode, 0)

if __name__ == '__main__':
    unittest.main()
