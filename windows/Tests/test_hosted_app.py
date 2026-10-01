"""Exercise the application's prepared playback path without opening audio hardware."""
import json
import os
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest


class HostedAppTests(unittest.TestCase):
    def setUp(self):
        self.directory = Path(self.enterContext(tempfile.TemporaryDirectory(prefix='hosted-app-', dir=os.environ['TMPDIR'])))
        self.exe = os.environ['SCREAMSEQ_TEST_EXE']

    def render(self, path=None):
        report = self.directory / 'render.json'
        args = [self.exe, '--offline-hosted-test', '--report', str(report)]
        if path:
            args += ['--project', str(path)]
        result = subprocess.run(args, timeout=90)
        self.assertEqual(result.returncode, 0)
        data = json.loads(report.read_text())
        self.assertTrue(data['finite'])
        self.assertTrue(data['documentUnchanged'])
        self.assertGreater(data['energy'], 0)
        self.assertLess(data['maxPartitionDelta'], 1e-6)
        self.assertEqual(data['rates'], [44100, 48000, 96000])
        self.assertEqual(data['partitions'], [17, 128, 4096, 8193])

    def test_demo_through_worker_preparation_and_callback(self):
        self.render()

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_REFERENCE_PROJECT'), 'actual Mac reference required')
    def test_actual_mac_routing_effects_and_notes(self):
        source = Path(os.environ['SCREAMSEQ_REFERENCE_PROJECT'])
        original = source.read_bytes()
        copy = self.directory / 'reference.screamseq'
        copy.write_bytes(original)
        self.render(copy)
        self.assertEqual(copy.read_bytes(), original)
        self.assertEqual(source.read_bytes(), original)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_REFERENCE_PROJECT'), 'actual Mac reference required')
    def test_unavailable_au_rejects_without_dry_substitution(self):
        tree = plistlib.loads(Path(os.environ['SCREAMSEQ_REFERENCE_PROJECT']).read_bytes())
        plugin = tree['plugins'][0]
        plugin.update(format='AU', type=int.from_bytes(b'aufx', 'big'), subtype=123, manufacturer=456,
                      classID='', path='', isInstrument=False, instrument=0)
        if tree['version'] >= 5:
            plugin['instrumentAssignments'] = []
        path = self.directory / 'unavailable.screamseq'
        original = plistlib.dumps(tree, fmt=plistlib.FMT_BINARY)
        path.write_bytes(original)
        report = self.directory / 'unavailable.json'
        result = subprocess.run([self.exe, '--offline-hosted-test', '--project', str(path), '--report', str(report)], timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(report.exists())
        self.assertEqual(path.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
