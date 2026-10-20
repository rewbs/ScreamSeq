"""Refuse unsafe or mismatched retained inputs before an app can launch."""
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import reuse_native
from project_tree import EvidenceError, digest, read_project, sha256


class RetainedSafetyTests(unittest.TestCase):
    def exchange_fixture(self, root):
        """A complete, hashed originating artifact, without a native process."""
        corpus = root / "corpus"
        (corpus / "fixtures").mkdir(parents=True)
        artifact = root / "artifact"
        artifact.mkdir()
        identity = dict(sourceCommit="1" * 40, executableSHA256="2" * 64,
                        buildReceiptSHA256="3" * 64, archiveSHA256="4" * 64)
        (artifact / "native-app-identity.json").write_text(json.dumps(identity))
        for name in ("01.screamseq", "04-complete-reference.screamseq"):
            (corpus / "fixtures" / name).write_bytes(
                plistlib.dumps(dict(name=name, opaque=b"\x00\xff", real=1.0), fmt=plistlib.FMT_BINARY))
        sources = sorted((corpus / "fixtures").glob("*.screamseq"))
        parent = self.save_leg(artifact / "parity-conformance", identity, sources)
        parent["inputSHA256"] = {path.name: sha256(path.read_bytes()) for path in sources}
        asset_dir = artifact / "parity-fixture-output/fixture"
        asset_dir.mkdir(parents=True)
        source = corpus / "fixtures/04-complete-reference.screamseq"
        for name in ("original.screamseq", "rendered.screamseq", "imported.screamseq"):
            (asset_dir / name).write_bytes(source.read_bytes())
        (asset_dir / "assets.json").write_text(json.dumps(dict(renders=1, imports=1)))
        (artifact / "screamseq-retained-editors.log").write_text("workspace-parity-fixture-tests ... Passed\n")
        for _, (key, directory, format_name, rows_key, name) in reuse_native.EXTENDED_LEGS.items():
            destination = artifact / "parity-conformance" / directory
            destination.mkdir()
            saved = destination / name
            saved.write_bytes(plistlib.dumps(dict(edited=directory, opaque=b"\x01\x80", signedZero=-0.0),
                                             fmt=plistlib.FMT_BINARY))
            row = dict(file=name, sha256=sha256(saved.read_bytes()), typedSHA256=digest(read_project(saved)),
                       comparison=dict(equal=True))
            journey = dict(format=format_name, passed=True, inputsAndOutputsUnchanged=True,
                           sourceSHA256=sha256(source.read_bytes()), exchangeInput=name, **{rows_key: [row]})
            report_path = destination / "report.json"
            report_path.write_text(json.dumps(journey))
            parent[key] = dict(passed=True, report=directory + "/report.json", sha256=sha256(report_path.read_bytes()))
        parent_path = artifact / "parity-conformance/report.json"
        parent_path.write_text(json.dumps(parent))
        return corpus, artifact, identity, parent_path

    def save_leg(self, directory, identity, sources):
        directory.mkdir(parents=True)
        (directory / "first").mkdir()
        (directory / "reopened").mkdir()
        rows = []
        for source in sources:
            for folder in ("first", "reopened"):
                (directory / folder / source.name).write_bytes(source.read_bytes())
            rows.append(dict(fixture=source.name, passed=True, originalToFirst=dict(equal=True),
                             firstToSecond=dict(equal=True), firstSave=dict(sha256=sha256(source.read_bytes())),
                             secondSave=dict(sha256=sha256(source.read_bytes()))))
        report = dict(format="screamseq-native-roundtrip-v1", binarySourceCommit=identity["sourceCommit"],
                      executableSHA256=identity["executableSHA256"], buildReceiptSHA256=identity["buildReceiptSHA256"],
                      inputsUnchanged=True, binaryUnchanged=True, originalCorpusUnchanged=True, fixtures=rows,
                      inputSHA256={path.name: sha256(path.read_bytes()) for path in sources})
        (directory / "report.json").write_text(json.dumps(report))
        return report

    def test_extended_exchange_requires_all_edited_journeys_and_preserves_their_lineage(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            corpus, artifact, identity, _ = self.exchange_fixture(root)
            with patch.object(reuse_native, "CORPUS", corpus):
                legs, lineage = reuse_native.input_legs(artifact, "windows", identity, extended=True)
                self.assertEqual(set(legs), {"no-edit", "edited", "metadata", "history", "envelope-bank"})
                self.assertEqual(set(lineage["editedJourneys"]), {"metadata", "history", "envelope-bank"})
                # Distinct journeys both use reopened.screamseq; don't overwrite
                # one another in the lineage fingerprint map.
                self.assertNotEqual(lineage["inputs"]["history"], lineage["inputs"]["envelope-bank"])
                exchange_dir = root / "mac-return"
                exchange = dict(format="screamseq-retained-run-v2", passed=True, mode="exchange",
                                appIdentity=identity, legs={}, lineage=lineage)
                for name, sources in legs.items():
                    self.save_leg(exchange_dir / name, identity, sources)
                    exchange["legs"][name] = dict(exitCode=0,
                        reportSHA256=sha256((exchange_dir / name / "report.json").read_bytes()))
                (exchange_dir / "retained-run.json").write_text(json.dumps(exchange))
                returned, _ = reuse_native.input_legs(exchange_dir, "exchange", identity, extended=True)
                for name in legs:
                    self.assertEqual([p.read_bytes() for p in returned[name]], [p.read_bytes() for p in legs[name]])
                # Two valid subreports with identical filenames must not be
                # interchangeable between different editing journeys.
                saved_lineage = exchange["lineage"]["inputs"]["history"]
                exchange["lineage"]["inputs"]["history"] = exchange["lineage"]["inputs"]["envelope-bank"]
                (exchange_dir / "retained-run.json").write_text(json.dumps(exchange))
                with self.assertRaises(EvidenceError):
                    reuse_native.input_legs(exchange_dir, "exchange", identity, extended=True)
                exchange["lineage"]["inputs"]["history"] = saved_lineage
                # A complete legacy pass must not silently satisfy the larger gate.
                exchange["format"] = "screamseq-retained-run-v1"
                (exchange_dir / "retained-run.json").write_text(json.dumps(exchange))
                with self.assertRaises(EvidenceError):
                    reuse_native.input_legs(exchange_dir, "exchange", identity, extended=True)

    def test_extended_journey_refuses_failed_missing_tampered_or_uncompared_saves(self):
        mutations = ("failed", "changed", "wrong-source", "missing", "tampered",
                     "uncompared", "typed", "unsafe-name", "wrong-stage", "duplicate")
        for mutation in mutations:
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                corpus, artifact, identity, parent_path = self.exchange_fixture(root)
                parent = json.loads(parent_path.read_text())
                report_path = artifact / "parity-conformance/edited-metadata/report.json"
                report = json.loads(report_path.read_text())
                if mutation == "failed":
                    report["passed"] = False
                elif mutation == "changed":
                    report["inputsAndOutputsUnchanged"] = False
                elif mutation == "wrong-source":
                    report["sourceSHA256"] = "0" * 64
                elif mutation == "missing":
                    del parent["editedMetadata"]
                elif mutation == "tampered":
                    (report_path.parent / report["exchangeInput"]).write_bytes(b"changed after qualification")
                elif mutation == "uncompared":
                    report["stages"][0].pop("comparison")
                elif mutation == "typed":
                    report["stages"][0]["typedSHA256"] = "0" * 64
                elif mutation == "unsafe-name":
                    report["exchangeInput"] = "../outside.screamseq"
                elif mutation == "wrong-stage":
                    report["exchangeInput"] = "clean-reopened.screamseq"
                elif mutation == "duplicate":
                    report["stages"].append(report["stages"][0].copy())
                report_path.write_text(json.dumps(report))
                if "editedMetadata" in parent:
                    parent["editedMetadata"]["sha256"] = sha256(report_path.read_bytes())
                parent_path.write_text(json.dumps(parent))
                with patch.object(reuse_native, "CORPUS", corpus), self.assertRaises(EvidenceError):
                    reuse_native.input_legs(artifact, "windows", identity, extended=True)

    def test_extended_exchange_without_input_never_launches(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(reuse_native, "native_main") as launch:
            root = Path(tmp)
            self.assertEqual(reuse_native.main(["--app-artifact", str(root), "--output", str(root / "result"),
                                               "--extended-exchange"]), 2)
            launch.assert_not_called()

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
