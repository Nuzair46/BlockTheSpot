"""End-to-end validator tests using synthetic assets, never Spotify code."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT/'out'/'tools'/('patch-tool.exe' if os.name == 'nt' else 'patch-tool')


def hex_bytes(text):
    return text.encode().hex(' ')


class ValidatorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        (self.folder/'Apps').mkdir()
        data = bytearray(0x600)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 0x80)
        data[0x80:0x84] = b'PE\0\0'
        struct.pack_into('<HH', data, 0x84, 0x8664, 1)
        struct.pack_into('<H', data, 0x94, 0xf0)
        section = 0x80 + 24 + 0xf0
        data[section:section+8] = b'.text\0\0\0'
        struct.pack_into('<IIII', data, section+8, 0x200, 0x1000, 0x200, 0x400)
        data[0x400:0x402] = b'\xaa\xbb'
        (self.folder/'Spotify.dll').write_bytes(data)
        self.js = 'globalThis.value=1;'
        self.css = '.x{display:flex}'
        with zipfile.ZipFile(self.folder/'Apps'/'xpui.spa', 'w') as archive:
            archive.writestr('test.js', self.js)
            archive.writestr('test.css', self.css)
        self.config = self.folder/'config.ini'
        self.config.write_text(f'''[Compatibility]
Spotify=1.0.0.0
[Developer]
Signature=AA BB
Value=FF
Offset=0
[URL_block]
1=/ads/
[Buffer_modify]
1=test.js
[test.js]
1=change
[change]
Signature_1={hex_bytes(self.js)}
Value_1=30
Offset_1={self.js.index('1')}
[Homepage_vbar]
Signature={hex_bytes(self.css)}
Value=6e 6f 6e 65
Offset=11
''')

    def validate(self):
        return subprocess.run([os.sys.executable, str(ROOT/'tools'/'validate_signatures.py'),
                               str(self.folder), '--config', str(self.config), '--engine', str(ENGINE)],
                              capture_output=True, text=True)

    def test_valid_assets_and_no_mutation(self):
        before = (self.folder/'Apps'/'xpui.spa').read_bytes()
        result = self.validate()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(before, (self.folder/'Apps'/'xpui.spa').read_bytes())

    def test_negative_offset_rejected_by_shared_engine(self):
        self.config.write_text(self.config.read_text().replace(f'Offset_1={self.js.index("1")}', 'Offset_1=-1'))
        result = self.validate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('invalid unsigned offset', result.stderr)

    def test_syntax_error_fails_validation(self):
        self.config.write_text(self.config.read_text().replace('Value_1=30', 'Value_1=29').replace(f'Offset_1={self.js.index("1")}', 'Offset_1=0'))
        result = self.validate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('SyntaxError', result.stderr)

    def test_ambiguous_signature_leaves_output_absent(self):
        self.config.write_text(self.config.read_text().replace('Signature_1='+hex_bytes(self.js), 'Signature_1=61'))
        source, output = self.folder/'input', self.folder/'output'
        source.write_bytes(b'aaa')
        result = subprocess.run([str(ENGINE), 'apply', str(self.config), 'test.js', str(source), str(output)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ambiguous', result.stderr)
        self.assertFalse(output.exists())
