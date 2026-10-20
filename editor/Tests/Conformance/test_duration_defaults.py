"""Checks for the evidence verifier; actual native behavior requires the app."""
import copy
import json
from pathlib import Path
import plistlib
import tempfile
import unittest

import duration_defaults
from project_tree import EvidenceError, read_project


class DurationEvidenceTests(unittest.TestCase):
    def client(self, mode):
        class Client:
            tree = None

            def call(self, method, params=None):
                params = params or {}
                if method == "document.get":
                    return dict(revision=1, data=dict(requiresSaveAs=False, loadWarnings=[]))
                if method == "document.open":
                    self.tree = read_project(Path(params["path"]))
                    return {}
                if method == "document.save":
                    tree = copy.deepcopy(self.tree)
                    commands = tree["native"]["performance"]["commands"]
                    if mode == "explicit":
                        # Materialize exactly the missing non-nudge field; the
                        # corpus's separate durationBeats representation stays.
                        for command in commands:
                            if command["kind"] not in (6, 7):
                                command.setdefault("duration", 0)
                    elif mode == "drop":
                        commands.pop(2)
                    elif mode == "unrelated":
                        tree["native"]["tracks"][0][1]["name"] = "Unexpected change"
                    with Path(params["path"]).open("xb") as stream:
                        stream.write(plistlib.dumps(tree, fmt=plistlib.FMT_BINARY, sort_keys=False))
                    return {}
                raise AssertionError(method)
        return Client()

    def test_accepts_only_the_two_declared_wire_representations(self):
        for mode in ("omitted", "explicit"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as folder:
                result = duration_defaults.run(self.client(mode), Path(folder) / "evidence")
                self.assertTrue(result["passed"])
                self.assertTrue(result["originalUnchanged"])
                self.assertTrue(result["inputsUnchanged"])
                self.assertEqual(len(result["cases"]), 3)
                self.assertTrue(all(len(row["saves"]) == 2 for row in result["cases"]))

    def test_rejects_lost_commands_and_unrelated_changes(self):
        for mode in ("drop", "unrelated"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as folder:
                output = Path(folder) / "evidence"
                with self.assertRaises(EvidenceError):
                    duration_defaults.run(self.client(mode), output)
                report = json.loads((output / "report.json").read_text())
                self.assertFalse(report["passed"])
                self.assertTrue(report["originalUnchanged"])
                self.assertTrue(report["inputsUnchanged"])
