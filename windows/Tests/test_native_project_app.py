"""Actual app startup against a current Mac fixture, with file-derived expectations."""
import os
import plistlib
from pathlib import Path
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'windows/Api'))
from client import Client, TransportError

REFERENCE = os.environ.get('SCREAMSEQ_REFERENCE_PROJECT', str(ROOT / 'editor/Tests/Fixtures/Parity20261009/fixtures/04-complete-reference.screamseq'))

@unittest.skipUnless(REFERENCE, 'SCREAMSEQ_REFERENCE_PROJECT is required for the actual Mac fixture')
class NativeProjectAppTests(unittest.TestCase):
    def test_actual_mac_project_restores_before_api_is_published(self):
        source = Path(REFERENCE)
        before = source.read_bytes()
        stored = plistlib.loads(before)
        native = stored['native']
        exe = os.environ.get('SCREAMSEQ_TEST_EXE', str(ROOT / 'bin/windows-editor/Release/ScreamSeq.exe'))
        process = subprocess.Popen([exe, '--project', str(source), '--inspection', '--automation', '--seconds', '30'])
        try:
            client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(process.pid), timeout=1)
            for _ in range(100):
                self.assertIsNone(process.poll(), 'project startup failed before API publication')
                try:
                    state = client.call('document.get')
                    break
                except TransportError:
                    time.sleep(.1)
            else:
                self.fail('Native project API did not become ready')
            self.assertEqual(state['data']['nativeSummary'], {
                'preciseNotes': len(native['preciseNotes']),
                'signalDefinitions': len(native['signalGraph']['library']),
                'envelopeTemplates': len(native['envelopeBank']['entries'])})
            self.assertEqual([(item['index'], item['id']) for item in state['data']['instruments']],
                             [(slot, item['id']) for slot, item in native['instruments']])
            self.assertEqual(len(state['data']['nativePlugins']), len(stored['plugins']))
            self.assertEqual(Path(client.call('context.get')['data']['file']), source.resolve())
            self.assertFalse(client.call('transport.get')['data']['playing'])
        finally:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
        self.assertEqual(source.read_bytes(), before)

if __name__ == '__main__':
    unittest.main()
