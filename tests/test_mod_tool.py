import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'out' / 'tools' / ('patch-tool.exe' if os.name == 'nt' else 'patch-tool')


class ModToolTests(unittest.TestCase):
    def test_sample_mod_and_disabled_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            config, source, output = (work / name for name in ('mod.ini', 'input', 'output'))
            sample = (ROOT / 'examples/mods/sample.ini').read_text()
            config.write_text(sample)
            def run(*args):
                return subprocess.run([str(ENGINE), *map(str, args)], capture_output=True, text=True)
            info = run('inspect-mod', config)
            self.assertEqual(info.returncode, 0, info.stderr)
            self.assertIn('ENABLED\t0', info.stdout)
            source.write_bytes(b'function demo(){return false;}')
            self.assertNotEqual(run('apply-mod', config, 'sample.js', source, output).returncode, 0)
            self.assertFalse(output.exists())
            config.write_text(sample.replace('Enable=0', 'Enable=1', 1))
            info = run('inspect-mod', config)
            self.assertEqual(info.returncode, 0, info.stderr)
            self.assertIn('NATIVE\tSpotify.dll', info.stdout)
            result = run('apply-mod', config, 'sample.js', source, output)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_bytes(), b'function demo(){return true ;}')
            self.assertEqual(source.read_bytes(), b'function demo(){return false;}')
            source.write_bytes(b'BTS_TEST')
            result = run('apply-mod', config, 'Spotify.dll', source, output)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_bytes(), b'BTS_PASS')
            self.assertEqual(source.read_bytes(), b'BTS_TEST')

    def test_failed_mod_does_not_write_output(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            config, source, output = (work / name for name in ('mod.ini', 'input', 'output'))
            config.write_text((ROOT / 'examples/mods/sample.ini').read_text().replace('Enable=0', 'Enable=1', 1))
            source.write_bytes(b'function unrelated(){}')
            result = subprocess.run([str(ENGINE), 'apply-mod', str(config), 'sample.js', str(source), str(output)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())
