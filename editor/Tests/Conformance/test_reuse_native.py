"""Refuse unsafe or mismatched retained inputs before an app can launch."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import reuse_native
from project_tree import EvidenceError, sha256


class RetainedSafetyTests(unittest.TestCase):
    def test_existing_evidence_is_not_changed_and_no_app_launches(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root / "output"
            output.mkdir()
            (output / "prior.txt").write_bytes(b"retained evidence")
            with patch.object(reuse_native, "native_main") as launch:
                self.assertEqual(reuse_native.main(["--app-artifact", str(root), "--output", str(output)]), 2)
                launch.assert_not_called()
            self.assertEqual(sorted(path.name for path in output.iterdir()), ["prior.txt"])
            self.assertEqual((output / "prior.txt").read_bytes(), b"retained evidence")

    def test_tampered_archive_is_refused_before_extraction_or_launch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "native-app-identity.json").write_text(json.dumps(dict(sourceCommit="1" * 40,
                archiveSHA256="0" * 64, executableSHA256="2" * 64, buildReceiptSHA256="3" * 64)))
            (root / "native-app.zip").write_bytes(b"tampered archive")
            with patch.object(reuse_native.sys, "platform", "win32"), patch.object(reuse_native, "native_main") as launch:
                self.assertEqual(reuse_native.main(["--app-artifact", str(root), "--output", str(root / "result")]), 2)
                launch.assert_not_called()
            self.assertFalse((root / "result/app").exists())

    def test_zip_escape_and_case_collision_never_extract_payload(self):
        for names in (("../outside.exe",), ("ScreamSeq.exe", "screamseq.exe"), ("C:/outside.exe",)):
            with self.subTest(names=names), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                archive = root / "app.zip"
                with zipfile.ZipFile(archive, "w") as bundle:
                    for name in names:
                        bundle.writestr(name, b"must not launch")
                with self.assertRaises(EvidenceError):
                    reuse_native.unpack(archive, root / "app")
                self.assertEqual(list((root / "app").iterdir()), [])
                self.assertFalse((root / "outside.exe").exists())

    def test_preceding_save_bytes_and_source_identity_are_required(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "first").mkdir()
            (root / "reopened").mkdir()
            saved = root / "first/rendered.screamseq"
            saved.write_bytes(b"qualified preceding save")
            reopened = root / "reopened/rendered.screamseq"
            reopened.write_bytes(saved.read_bytes())
            identity = dict(sourceCommit="1" * 40, executableSHA256="2" * 64, buildReceiptSHA256="3" * 64)
            report = dict(format="screamseq-native-roundtrip-v1", binarySourceCommit=identity["sourceCommit"],
                executableSHA256=identity["executableSHA256"], buildReceiptSHA256=identity["buildReceiptSHA256"],
                inputsUnchanged=True, binaryUnchanged=True, originalCorpusUnchanged=True,
                fixtures=[dict(fixture=saved.name, passed=True, originalToFirst=dict(equal=True),
                               firstToSecond=dict(equal=True), firstSave=dict(sha256=sha256(saved.read_bytes())),
                               secondSave=dict(sha256=sha256(reopened.read_bytes())))])
            report_path = root / "report.json"
            report_path.write_text(json.dumps(report))
            self.assertEqual(reuse_native.prior_leg(report_path, identity, [saved.name]), [saved])
            with self.assertRaises(EvidenceError):
                reuse_native.prior_leg(report_path, {**identity, "sourceCommit": "4" * 40}, [saved.name])
            saved.write_bytes(b"replacement must not be exchanged")
            with self.assertRaises(EvidenceError):
                reuse_native.prior_leg(report_path, identity, [saved.name])

    def test_build_or_product_changes_cannot_be_called_tooling_only(self):
        for path in ("editor/NativeSong.cpp", "soundlib/Fastmix.cpp", "windows/CMakeLists.txt",
                     "mac/build.sh", "windows/App/Main.cpp", "mac/Tools/resonance-api.schema.json"):
            self.assertFalse(reuse_native.tooling_only(path), path)
        for path in ("doc/api/codec-observations.json", "editor/Tests/Conformance/reuse_native.py",
                     ".github/workflows/ScreamSeq-Retained.yml", "windows/Tests/test_recording.py"):
            self.assertTrue(reuse_native.tooling_only(path), path)


if __name__ == "__main__":
    unittest.main()
