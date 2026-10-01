"""Actual app startup and persistence paths against the user's disposable Mac fixture."""
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'windows/Api'))
from client import Client, TransportError

REFERENCE = os.environ.get('SCREAMSEQ_REFERENCE_PROJECT')

@unittest.skipUnless(REFERENCE, 'SCREAMSEQ_REFERENCE_PROJECT is required for the actual Mac fixture')
class NativeProjectAppTests(unittest.TestCase):
    def test_actual_mac_project_restores_before_api_is_published(self):
        source = Path(REFERENCE)
        before = source.read_bytes()
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
                'preciseNotes': 4, 'signalDefinitions': 2, 'envelopeTemplates': 3})
            self.assertEqual(len(state['data']['instruments']), 7)
            self.assertEqual(len(state['data']['nativePlugins']), 1)
            first = client.call('pattern.get', {'pattern': 0, 'rowCount': 1, 'channelCount': 1})['data']['cells'][0]
            self.assertEqual(first['note'], 0, 'Mac precise-note fixture differs from the ordinary demo cell')
            self.assertEqual(Path(client.call('context.get')['data']['file']), source.resolve())
            self.assertFalse(client.call('transport.get')['data']['playing'])
        finally:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
        self.assertEqual(source.read_bytes(), before)

if __name__ == '__main__':
    unittest.main()
