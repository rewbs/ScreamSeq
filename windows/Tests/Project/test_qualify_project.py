"""Generated regression cases for the qualification utility, not real songs."""
import copy
import hashlib
import json
import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from test_native_project import PROJECT_DIR, generated_project
import qualify_project


class QualificationUtilityTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR"))
        self.addCleanup(scratch.cleanup)
        self.directory = Path(scratch.name)
        environment = patch.dict(os.environ, {"TMPDIR": str(self.directory)})
        environment.start()
        self.addCleanup(environment.stop)
        self.source = self.directory / "generated.screamseq"

    def test_cli_reports_verified_disk_roundtrip_without_touching_source(self):
        root = generated_project()
        root["unknownExtension"] = {"bytes": bytes(range(256)), "values": [True, 7, "preserve"]}
        data = plistlib.dumps(root, fmt=plistlib.FMT_BINARY)
        self.source.write_bytes(data)
        result = subprocess.run([sys.executable, "-B", str(PROJECT_DIR / "qualify_project.py"),
                                 str(self.source)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.strip(), "qualification CLI report is missing")
        report = json.loads(result.stdout)
        self.assertEqual(report["source"]["sha256"], hashlib.sha256(data).hexdigest())
        self.assertTrue(all(report["checks"].values()))
        self.assertIn("unknownExtension", report["preserved_top_level_fields"])
        self.assertEqual(report["snapshot_magic"], "RSONGS2\u0000")
        self.assertGreater(report["snapshot_sections"]["timing"]["bytes"], 0)
        self.assertFalse(report["song_semantics_validated"])
        self.assertEqual(self.source.read_bytes(), data)
        self.assertEqual(list(self.directory.iterdir()), [self.source])

    def test_rejects_plist_scalar_type_or_float_bit_changes(self):
        # Fault injection models a lossy serializer; ordinary Python == would
        # silently accept False -> 0, 1.0 -> 1, and -0.0 -> +0.0.
        for original, altered in ((False, 0), (1.0, 1), (-0.0, 0.0)):
            with self.subTest(original=repr(original), altered=repr(altered)):
                # plistlib deduplicates equal floats. Put this scalar first so
                # a generated +0.0 elsewhere cannot normalize the -0.0 input.
                root = {"unknownExtension": {"value": original}, **generated_project()}
                data = plistlib.dumps(root, fmt=plistlib.FMT_BINARY, sort_keys=False)
                self.source.write_bytes(data)
                changed = copy.deepcopy(root)
                changed["unknownExtension"]["value"] = altered
                with patch.object(qualify_project.native_project, "dumps", return_value=
                                  plistlib.dumps(changed, fmt=plistlib.FMT_BINARY, sort_keys=False)):
                    with self.assertRaisesRegex(ValueError, "tree"):
                        qualify_project.qualify(self.source)
                self.assertEqual(self.source.read_bytes(), data)
                self.assertEqual(list(self.directory.iterdir()), [self.source])

    def test_cli_failures_emit_no_success_report_or_traceback(self):
        future = generated_project()
        future["native"]["version"] = 15
        for data in (None, b"not a plist", plistlib.dumps(future, fmt=plistlib.FMT_BINARY)):
            with self.subTest(data_type=type(data).__name__):
                if data is not None:
                    self.source.write_bytes(data)
                result = subprocess.run([sys.executable, "-B", str(PROJECT_DIR / "qualify_project.py"),
                                         str(self.source)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")
                self.assertIn("Project roundtrip qualification failed:", result.stderr)
                self.assertNotIn("Traceback", result.stderr)
                self.assertEqual(list(self.directory.iterdir()), [] if data is None else [self.source])
                if data is not None:
                    self.assertEqual(self.source.read_bytes(), data)

    def test_rejects_in_place_serializer_field_loss(self):
        data = plistlib.dumps(generated_project(), fmt=plistlib.FMT_BINARY)
        self.source.write_bytes(data)
        encode = qualify_project.native_project.dumps

        def lossy_encode(root):
            del root["native"]["envelopeBank"]
            return encode(root)

        with patch.object(qualify_project.native_project, "dumps", side_effect=lossy_encode):
            with self.assertRaisesRegex(ValueError, "tree"):
                qualify_project.qualify(self.source)
        self.assertEqual(self.source.read_bytes(), data)
        self.assertEqual(list(self.directory.iterdir()), [self.source])


if __name__ == "__main__":
    unittest.main()
