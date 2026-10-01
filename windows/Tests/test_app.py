"""Runs only the disposable development executable and its own demo document."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import sys
import time
import unittest


class AppTests(unittest.TestCase):
    def test_idle_rendering_sleeps_and_edits_wake_it(self):
        root = Path(__file__).resolve().parents[2]
        sys.path.insert(0, str(root / 'windows/Api'))
        from client import Client, TransportError
        exe = Path(os.environ['SCREAMSEQ_TEST_EXE'])
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as folder:
            report = Path(folder) / 'idle.json'
            process = subprocess.Popen([str(exe), '--inspection', '--automation', '--seconds', '4', '--report', str(report)])
            try:
                client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(process.pid), timeout=5)
                for _ in range(100):
                    try:
                        client.call('document.get')
                        break
                    except TransportError:
                        time.sleep(.02)
                else:
                    self.fail('owned app did not start')
                time.sleep(.25)
                # Reads must not keep invalidating an unchanged native view.
                for _ in range(20):
                    client.call('sample.get', {'sample': 1})
                revision = client.call('document.get')['revision']
                client.call('document.patch', {'expectedRevision': revision, 'title': 'Wake after idle'})
                time.sleep(.25)
                context = client.call('context.get')
                client.call('context.set', {'expectedRevision': context['revision'],
                    'expectedContext': context['data']['contextRevision'], 'row': 4})
                self.assertEqual(process.wait(timeout=10), 0)
                data = json.loads(report.read_text())
                self.assertGreaterEqual(data['drawnFrames'], 3)
                self.assertLess(data['drawnFrames'], 20)
                self.assertGreater(data['idleWaits'], 1)
                self.assertLessEqual(data['textCacheEntries'], 4096)
                self.assertGreater(data['textCacheHits'], data['textCacheMisses'])
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=10)

    def test_offline_shared_engine_and_callback_partitions(self):
        root = Path(__file__).resolve().parents[2]
        exe = Path(os.environ.get("SCREAMSEQ_TEST_EXE", root / "bin/windows-arm64/Release/ScreamSeq.exe"))
        self.assertTrue(exe.is_file(), "Windows native app has not been built")
        with tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR")) as folder:
            report = Path(folder) / "offline.json"
            result = subprocess.run([str(exe), "--offline-test", "--report", str(report)], timeout=60)
            self.assertEqual(result.returncode, 0)
            data = json.loads(report.read_text())
            self.assertTrue(data["finite"])
            # Same rounding bound as mac/Tests/SongTimingTests.cpp; exact tick
            # partition timing is checked by that reused C++ regression suite.
            self.assertLess(data["maxPartitionDelta"], 1e-6)
            self.assertGreater(data["energy"], 0.1)
            self.assertEqual(data["rates"], [44100, 48000, 96000])
            self.assertEqual(data["partitions"], [17, 128, 4096])


if __name__ == "__main__":
    unittest.main()
