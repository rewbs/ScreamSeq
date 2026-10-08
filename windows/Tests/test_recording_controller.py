"""Recording worker transactions and timed rendering on the inherited private desktop."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class RecordingControllerTests(unittest.TestCase):
    def test_recording_clock_lifecycle_recovery_and_atomic_commit(self):
        executable = Path(os.environ['SCREAMSEQ_TEST_EXE']).with_name('document-controller-tests.exe')
        with tempfile.TemporaryDirectory(prefix='recording-controller-', dir=os.environ['TMPDIR']) as directory:
            result = subprocess.run([str(executable), '--recording', directory],
                                    capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS recording worker:', result.stdout)
