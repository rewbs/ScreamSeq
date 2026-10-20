"""Fixture setup must expose actual requests and preserve uncertain outcomes."""
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest

from fixture_loader import FixtureLoader
from project_tree import EvidenceError


class FixtureLoaderTests(unittest.TestCase):
    def test_windows_uses_guarded_open_without_staging(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "source.screamseq"
            source.write_bytes(b"original")
            calls, journal = [], []
            def call(method, params=None):
                calls.append((method, params))
                return dict(revision="current")
            result = FixtureLoader(SimpleNamespace(), call, journal).load(source)
            self.assertEqual(result, source.resolve())
            self.assertEqual(calls, [("document.get", None), ("document.open",
                dict(expectedRevision="current", path=str(source.resolve()), discard=True))])
            self.assertEqual(journal[0]["method"], "document.open")

    def test_mac_recovery_uses_exact_owned_copy_and_real_error_without_retry(self):
        for failure in (None, RuntimeError("unknown transport outcome")):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                source, recovery = root / "input.screamseq", root / "Recovery"
                source.write_bytes(b"exact opaque bytes")
                recovery.mkdir()
                calls, journal = [], []
                def call(method, params=None):
                    calls.append((method, params))
                    if method == "document.get":
                        return dict(revision="fresh")
                    self.assertEqual(method, "recovery.restore")
                    self.assertEqual(set(params), {"id", "expectedRevision"})
                    self.assertEqual(params["expectedRevision"], "fresh")
                    self.assertEqual((recovery / params["id"]).read_bytes(), source.read_bytes())
                    if failure:
                        raise failure
                    return dict(revision="restored")
                loader = FixtureLoader(SimpleNamespace(fixture_recovery_directory=recovery), call, journal)
                if failure:
                    with self.assertRaises(RuntimeError) as raised:
                        loader.load(source)
                    self.assertIs(raised.exception, failure)
                else:
                    self.assertEqual(loader.load(source).parent, recovery)
                self.assertEqual(len(calls), 2)
                self.assertEqual(journal[0]["method"], "recovery.restore")
                self.assertTrue(journal[0]["sourceUnchanged"])
                self.assertTrue(journal[0]["effectiveSourceUnchanged"])

    def test_load_cannot_silently_modify_staged_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, recovery = root / "input.screamseq", root / "Recovery"
            source.write_bytes(b"keep")
            recovery.mkdir()
            def call(method, params=None):
                if method == "document.get":
                    return dict(revision="current")
                (recovery / params["id"]).write_bytes(b"wrong")
            journal = []
            with self.assertRaises(EvidenceError):
                FixtureLoader(SimpleNamespace(fixture_recovery_directory=recovery), call, journal).load(source)
            self.assertFalse(journal[0]["effectiveSourceUnchanged"])
            self.assertEqual(source.read_bytes(), b"keep")


if __name__ == "__main__":
    unittest.main()
