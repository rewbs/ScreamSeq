"""Safety boundaries for the actual-app runner; native behavior is tested live."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import native_roundtrip
from project_tree import CORPUS, sha256


class RoundtripSafetyTests(unittest.TestCase):
    def test_uncertain_save_stops_without_retry_or_replacement(self):
        class BrokenTransport:
            def __init__(self):
                self.methods = []

            def call(self, method, params=None):
                self.methods.append(method)
                if method == "document.save":
                    raise RuntimeError("Owned uncertain save result")
                return dict(revision="r:1", data={})

        source = next((CORPUS / "fixtures").glob("*.screamseq"))
        client, row = BrokenTransport(), {}
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(RuntimeError, "uncertain save"):
                native_roundtrip.exercise(client, source, Path(tmp) / "first.screamseq",
                                          Path(tmp) / "second.screamseq", row)
        self.assertEqual(client.methods.count("document.open"), 1)
        self.assertEqual(client.methods.count("document.save"), 1)
        self.assertEqual(client.methods[-1], "document.save")
        self.assertIn("error", row["requests"][-1])

    def test_mismatched_binary_and_existing_output_never_launch(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            binary = directory / "not-an-app"
            binary.write_bytes(b"explicit expected executable")
            receipt = directory / "receipt.json"
            receipt.write_text(json.dumps({"testOnly": True}), encoding="utf-8")
            output = directory / "retained"
            output.mkdir()
            marker = output / "report.json"
            marker.write_bytes(b"prior evidence must survive")
            args = ["--executable", str(binary), "--binary-source-commit", "1" * 40,
                    "--build-receipt", str(receipt), "--output", str(output)]
            with patch.object(native_roundtrip, "owned_client") as launch:
                for value in ("0" * 64, sha256(binary.read_bytes())):
                    self.assertEqual(native_roundtrip.main(args + ["--binary-sha256", value]), 2)
                source = next((CORPUS / "fixtures").glob("*.screamseq"))
                self.assertEqual(native_roundtrip.main(args + ["--binary-sha256", sha256(binary.read_bytes()),
                    "--input-file", str(source), "--input-file", str(source)]), 2)
                launch.assert_not_called()
            self.assertEqual(marker.read_bytes(), b"prior evidence must survive")


if __name__ == "__main__":
    unittest.main()
