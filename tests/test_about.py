"""Exercise the About patch without distributing Spotify's desktop bundle."""
import configparser
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.generate_about_patch import ORIGINAL_MAP, ORIGINAL_CREDITS, CREDITS_HTML, patches

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'out' / 'tools' / ('patch-tool.exe' if os.name == 'nt' else 'patch-tool')


class AboutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / 'before.js'
        self.output = Path(self.temp.name) / 'after.js'
        # Translation and JSX stand-ins expose the output and existing props.
        self.fixture = '''const u={Ru:{get:(key,...args)=>[key,...args].join(":")}};
const r={jsx:(type,props)=>({type,props})},a={E:"Text"},A={y:"FormattedText"},p="2026";
''' + ORIGINAL_MAP + ';\nconst credits=' + ORIGINAL_CREDITS + ''';
console.log(JSON.stringify({platforms:Object.fromEntries(k),credits}));
'''
        self.source.write_text(self.fixture, encoding='utf-8')

    def apply_patch(self):
        return subprocess.run([str(ENGINE), 'apply-mod', str(ROOT / 'patches' / 'blockthespot.ini'),
                               'xpui-desktop-modals.js', str(self.source), str(self.output)],
                              capture_output=True, text=True)

    def evaluate(self, path):
        return json.loads(subprocess.check_output(['node', str(path)], text=True))

    def test_preserves_existing_details_and_appends_links(self):
        before = self.evaluate(self.source)
        result = self.apply_patch()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.source.stat().st_size, self.output.stat().st_size)
        after = self.evaluate(self.output)
        self.assertEqual(before['platforms'], after['platforms'])
        self.assertEqual(len(after['platforms']), 6)
        self.assertEqual(after['platforms']['Win32_x86_64'], 'desktop-about.platform-win-x86-64')
        original = before['credits']['props']['children']['props']['source']
        rendered = after['credits']['props']['children']['props']['source']
        self.assertEqual(rendered, original + CREDITS_HTML)

        class Links(HTMLParser):
            def __init__(self):
                super().__init__()
                self.hrefs = []

            def handle_starttag(self, tag, attrs):
                if tag == 'a':
                    self.hrefs.append(dict(attrs).get('href'))

        links = Links()
        links.feed(rendered)
        self.assertEqual(links.hrefs, ['https://github.com/Nuzair46/BlockTheSpot',
                                      'https://discord.gg/eYudMwgYtY'])
        after['credits']['props']['children']['props']['source'] = original
        self.assertEqual(before['credits'], after['credits'])

    def test_changed_dialog_rejects_both_writes(self):
        self.source.write_text(self.fixture.replace('about.copyright', 'about.changed'), encoding='utf-8')
        original = self.source.read_bytes()
        result = self.apply_patch()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.source.read_bytes(), original)

    def test_readable_source_matches_distributed_config(self):
        ini = configparser.ConfigParser(interpolation=None)
        ini.read(ROOT / 'patches' / 'blockthespot.ini')
        for i, (signature, value) in enumerate(patches(), 1):
            self.assertEqual(bytes.fromhex(ini['about_blockthespot'][f'Signature_{i}']), signature)
            self.assertEqual(bytes.fromhex(ini['about_blockthespot'][f'Value_{i}']), value)
            self.assertEqual(ini['about_blockthespot'][f'Offset_{i}'], '0')
