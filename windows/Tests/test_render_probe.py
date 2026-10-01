"""Real GPU/window smoke test; does not assert sustained presentation success."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class RenderProbeTests(unittest.TestCase):
    def test_real_window_draws_and_reports_gpu(self):
        root = Path(__file__).resolve().parents[2]
        exe = Path(os.environ.get("SCREAMSEQ_RENDER_PROBE", root / "bin/windows-arm64/Release/renderer-probe.exe"))
        self.assertTrue(exe.is_file(), "native render probe has not been built")
        with tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR")) as temp:
            report = Path(temp) / "probe.json"
            result = subprocess.run([str(exe), "--frames", "120", "--report", str(report)],
                                    capture_output=True, text=True, timeout=40)
            self.assertEqual(result.returncode, 0, result.stderr)
            data = json.loads(report.read_text())
            self.assertEqual(data["drawnFrames"], 120)
            self.assertTrue(data["adapter"])
            self.assertGreater(data["clientWidth"], 0)
            self.assertGreater(data["clientHeight"], 0)
            self.assertEqual(data["renderPath"], "D3D11/DXGI flip + Direct2D/DirectWrite")
            self.assertEqual(len(data["cpuDrawMicros"]), 120)
            self.assertFalse(data["sustainedPresentationQualified"])


if __name__ == "__main__":
    unittest.main()
