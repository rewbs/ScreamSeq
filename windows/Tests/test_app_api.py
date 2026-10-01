"""Actual disposable app/API integration, no user project or audio device."""
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'windows/Api'))
from client import Client, ApiError, TransportError


class AppApiTests(unittest.TestCase):
    def test_real_document_reads_and_guarded_transport(self):
        exe = Path(os.environ.get('SCREAMSEQ_TEST_EXE', ROOT / 'bin/windows-arm64/Release/ScreamSeq.exe'))
        process = subprocess.Popen([str(exe), '--inspection', '--automation', '--seconds', '30'])
        try:
            client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(process.pid), timeout=1)
            for attempt in range(50):
                self.assertIsNone(process.poll(), 'app exited before its API was ready')
                try:
                    doc = client.call('document.get')
                    break
                except TransportError:
                    time.sleep(.1)
            else:
                self.fail('app pipe did not become ready')
            self.assertEqual(doc['data']['title'], 'Midnight Circuit')
            self.assertEqual(doc['data']['channels'], 8)
            self.assertTrue(client.call('api.describe')['data']['musicalEditing'])
            pattern = client.call('pattern.get', {'pattern': 0, 'rowCount': 4})['data']
            self.assertEqual(len(pattern['cells']), 32)
            self.assertTrue(any(cell['note'] for cell in pattern['cells']))
            revision = doc['revision']
            with self.assertRaises(ApiError) as stale:
                client.call('transport.play', {'expectedRevision': 'stale'})
            self.assertEqual(stale.exception.code, -32001)
            with self.assertRaises(ApiError) as unavailable:
                client.call('transport.play', {'expectedRevision': revision})
            self.assertEqual(unavailable.exception.code, -32003)
            client.call('transport.stop', {'expectedRevision': revision})
            transport = client.call('transport.get')['data']
            self.assertFalse(transport['playing'])
            self.assertFalse(transport['audioActive'])
            self.assertEqual(transport['voicePositions'], [])
            self.assertEqual(client.call('document.get')['revision'], revision)
            with self.assertRaises(ApiError) as unsupported:
                client.call('pattern.apply', {'expectedRevision': revision})
            self.assertEqual(unsupported.exception.code, -32602)
            graph = client.call('graph.create', {'expectedRevision': revision, 'name': 'App graph'})
            self.assertTrue(graph['data']['wouldChange'])
            self.assertEqual(client.call('graph.get')['data']['library'][0]['id'], graph['data']['graph'])
            with self.assertRaises(ApiError) as stale:
                client.call('graph.create', {'expectedRevision': revision})
            self.assertEqual(stale.exception.code, -32001)
        finally:
            # Terminate only the PID created by this test, never discovery's first app.
            process.terminate()
            process.wait(timeout=10)


if __name__ == '__main__':
    unittest.main()
