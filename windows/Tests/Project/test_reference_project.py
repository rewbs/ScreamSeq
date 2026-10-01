"""Opt-in, hash-pinned Mac fixture; never synthesize or modify the input.

Set SCREAMSEQ_REFERENCE_PROJECT to the supplied UI reference pack's
reference.screamseq. The binary stays outside the repository.
"""
import hashlib
import importlib
import importlib.util
import os
from pathlib import Path
import plistlib
import sys
import unittest

PROJECT_DIR = Path(__file__).resolve().parents[2] / "Project"
sys.path.insert(0, str(PROJECT_DIR))
REFERENCE_SHA256 = "96ec9f809ef95a113d16613c29e73841e1f4a64ecc129672e5a48e548e587cd7"
REFERENCE_PATH = os.environ.get("SCREAMSEQ_REFERENCE_PROJECT")


@unittest.skipUnless(REFERENCE_PATH, "set SCREAMSEQ_REFERENCE_PROJECT to the supplied Mac fixture")
class MacReferenceProjectTests(unittest.TestCase):
    def test_supplied_mac_reference_container_roundtrip(self):
        source = Path(REFERENCE_PATH)
        original = source.read_bytes()
        self.assertEqual(hashlib.sha256(original).hexdigest(), REFERENCE_SHA256,
                         "not the unmodified supplied reference.screamseq")
        self.assertEqual(len(original), 63474)
        self.assertIsNotNone(importlib.util.find_spec("qualify_project"),
                             "read-only on-disk roundtrip qualification utility is missing")
        qualifier = importlib.import_module("qualify_project")
        report = qualifier.qualify(source)
        self.assertEqual(source.read_bytes(), original)
        self.assertEqual(report["scope"], "python-container-roundtrip-only")
        self.assertEqual(report["source"]["sha256"], REFERENCE_SHA256)
        self.assertEqual(report["container_version"], 4)
        self.assertEqual(report["native_metadata_version"], 14)
        self.assertEqual(report["snapshot_magic"], "RSONGS1\u0000")
        self.assertEqual(report["sequence"], 0)
        self.assertEqual(report["plugin_count"], 1)
        self.assertEqual(report["checks"], {
            "decoded_tree_equal": True,
            "source_unchanged": True,
            "snapshot_bytes_equal": True,
            "snapshot_sections_equal": True,
            "repacked_file_reopened": True,
        })
        for key in ("song_semantics_validated", "windows_app_restore_validated",
                    "mac_reopen_validated", "audio_fidelity_validated"):
            self.assertIs(report[key], False)
        self.assertEqual(report["snapshot_sections"], {
            "module": {"bytes": 53937, "sha256": "523873ddcfd767b3af04016acc3e853b5a6f6fee284bd016df1c89ad6cf998d0"},
            "samples": {"bytes": 3453, "sha256": "6a78d9a8b6e36330bb8d6decab2be1c2e5619074b021070b229bd9ce172dc347"},
            "timing": {"bytes": 0, "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        })

        # These are observed persisted values, NOT musical restoration checks.
        root = plistlib.loads(original)
        native = root["native"]
        self.assertEqual(report["preserved_top_level_fields"], sorted(root))
        self.assertEqual(report["preserved_native_fields"], sorted(native))
        self.assertEqual(native["nextID"], 38)
        self.assertEqual({key: len(native[key]) for key in (
            "patterns", "tracks", "samples", "instruments", "sequences",
            "preciseNotes", "automation")}, {
                "patterns": 1, "tracks": 8, "samples": 4, "instruments": 7,
                "sequences": 1, "preciseNotes": 4, "automation": 1})
        self.assertEqual([note["position"] for note in native["preciseNotes"]],
                         [0, 13107, 29491, 49152])
        self.assertEqual([note["velocity"] for note in native["preciseNotes"]],
                         [45, 64, 91, 117])
        graph = native["signalGraph"]
        self.assertEqual([(g["id"], g["name"], len(g["nodes"])) for g in graph["library"]],
                         [("n20", "Motion filter", 5), ("n26", "Crunch accent", 3)])
        self.assertEqual([c["kind"] for c in graph["commands"]], ["row", "start", "wet", "stop"])
        self.assertEqual(graph["assignments"], [
            {"amount": 0.6, "graph": "n20", "wet": 0.8, "target": "n2"}])
        self.assertEqual(graph["instrumentAssignments"], [])
        graph_plugins = [node["plugin"] for g in graph["library"] for node in g["nodes"]
                         if "plugin" in node]
        self.assertEqual([(p["format"], p["classID"], p["state"]) for p in graph_plugins], [
            ("Built-in", "resonance.digital-filter.v1", ""),
            ("Built-in", "resonance.distortion.v1", "")])
        envelopes = [envelope for g in graph["library"] for node in g["nodes"]
                     for envelope in node.get("envelopes", [])]
        self.assertEqual(len(envelopes), 1)
        self.assertEqual(envelopes[0]["pattern"], "n1")
        self.assertEqual(envelopes[0]["points"][0]["formula"], "mix(start,end,t^2)")
        bank = native["envelopeBank"]
        self.assertEqual([(entry["id"], entry["name"]) for entry in bank["entries"]], [
            ("n16", "Slow bloom"), ("n18", "Unlinked library copy"), ("n37", "Pluck contour")])
        self.assertEqual(bank["links"], [
            {"target": {"kind": "parameter", "owner": "n17", "pattern": ""},
             "template": "n16", "span": 16384},
            {"target": {"kind": "volume", "owner": "n30", "pattern": ""},
             "template": "n37", "span": 48}])
        for entry in bank["entries"]:
            self.assertEqual(entry["shape"]["markers"], [0, 0, 0, 0, 4294967295])
        plugin = root["plugins"][0]
        self.assertEqual((plugin["format"], plugin["classID"], plugin["instanceID"], plugin["state"]),
                         ("Built-in", "resonance.gainer.v1", "097DAD08-D422-4700-B432-41E4586A9EA0", b""))
        self.assertEqual(native["automation"][0]["plugin"], plugin["instanceID"])
        self.assertEqual(native["automation"][0]["points"][0][3], "mix(start,end,t^2)")
        self.assertEqual(root["automation"], [])
        self.assertNotIn("recoveryTake", root)


if __name__ == "__main__":
    unittest.main()
