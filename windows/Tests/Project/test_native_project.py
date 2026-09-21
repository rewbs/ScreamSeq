"""Generated plist fixtures, NOT Mac-produced or playable-song fixtures."""
import base64
import datetime
import importlib
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

PROJECT_DIR = Path(__file__).resolve().parents[2] / "Project"
sys.path.insert(0, str(PROJECT_DIR))


def snapshot(timing=True):
    # Exact SampleArchive.cpp framing; inner sections intentionally opaque.
    module = b"opaque module qualification payload\x00\xff"
    samples = bytes(range(256)) + b"\x00\x80\xff\x7f\x00\x00"
    clock = b"opaque timing payload\x00" if timing else b""
    lengths = [len(module), len(samples)] + ([len(clock)] if timing else [])
    return (b"RSONGS2\0" if timing else b"RSONGS1\0") + struct.pack(
        "<" + "I" * len(lengths), *lengths
    ) + module + samples + clock


def entity(identifier, name):
    return {"id": identifier, "name": name, "annotation": "", "color": 0}


def generated_project():
    """Field names/types follow the inspected Mac encoder, including metadata 14."""
    au = {"format": "AU", "type": 1635085685, "subtype": 1234,
          "manufacturer": 5678, "name": "Unavailable AU", "path": "",
          "classID": "", "isInstrument": False, "bypass": False,
          "instrument": 0, "instrumentAssignments": [],
          "instanceID": "stable-au-instance", "state": b"\x00\xffAU state\x80",
          "auxiliaryInputs": [], "auxiliaryOutputs": []}
    recipe = {k: au[k] for k in ("format", "type", "subtype", "manufacturer", "name", "path", "classID")}
    recipe.update(state=base64.b64encode(au["state"]).decode("ascii"), inputs=[], outputs=[])
    graph = {"library": [{"id": "n20", "number": 1, "name": "Graph", "nodes": [
        {"id": "n21", "kind": "plugin", "name": "AU node", "x": 0.0, "y": 0.0,
         "rate": 1.0, "phase": 0.0, "attack": .01, "release": .1,
         "controller": 1, "plugin": recipe}], "audio": [], "modulation": []}],
        "assignments": [], "instrumentAssignments": [], "commands": [],
        "lanes": [], "inputs": [], "outputs": [], "layout": []}
    native = {"version": 14, "nextID": 100,
              "patterns": [[0, entity("n1", "Pattern")]],
              "tracks": [[0, entity("n2", "Track")]],
              "samples": [[1, entity("n3", "Sample")]],
              "instruments": [[1, entity("n4", "Instrument")]],
              "sequences": [{"info": entity("n5", "Sequence"), "orders": [entity("n6", "Order")]}],
              "automation": [], "mixer": {"buses": [], "instruments": [], "sidechains": []},
              "noteTracks": [], "columnMutes": [], "preciseNotes": [],
              "performance": {"columns": [], "bindings": [], "commands": []},
              "signalGraph": graph,
              "envelopeBank": {"entries": [{"id": "n30", "name": "Envelope", "shape": {
                  "span": 256, "rowsPerBeat": 4, "instrument": True, "flags": 1,
                  "markers": [0, 0, 0, 0, 4294967295],
                  "points": [{"position": 0, "value": 1.0, "curve": "linear"}]}}],
                  "links": [{"target": {"kind": "volume", "owner": "n4", "pattern": ""},
                             "template": "n30", "span": 256}]}}
    return {"version": 5, "native": native, "sequence": 0, "module": snapshot(),
            "plugins": [au], "automation": [[0, 7, .5, 48000]]}


class NativeProjectTests(unittest.TestCase):
    def codec(self):
        self.assertIsNotNone(importlib.util.find_spec("native_project"),
                             "portable native project codec is not implemented")
        return importlib.import_module("native_project")

    def test_binary_plist_roundtrip_keeps_snapshot_and_unknown_data(self):
        codec = self.codec()
        root = generated_project()
        root["opaqueExtension"] = {"blob": bytes(range(256)), "items": [True, False, 3.5, "café 🎵"],
                                   "date": datetime.datetime(2026, 1, 2, 3, 4, 5), "large": 2**63 + 7}
        root["native"]["unknownNative"] = {"state": b"\x00\xff"}
        root["plugins"][0]["vendorUnknown"] = [b"opaque", {"value": 42}]
        source = plistlib.dumps(root, fmt=plistlib.FMT_BINARY, sort_keys=False)
        loaded = codec.loads(source)
        self.assertEqual(root, loaded)
        encoded = codec.dumps(loaded)
        self.assertTrue(encoded.startswith(b"bplist00"))
        decoded = plistlib.loads(encoded)
        self.assertEqual(root, decoded)
        self.assertEqual(root["module"], decoded["module"])
        self.assertEqual(root["plugins"][0], decoded["plugins"][0])
        self.assertEqual(root["native"]["signalGraph"], decoded["native"]["signalGraph"])

    def test_rejects_unsupported_or_noninteger_versions_on_load_and_dump(self):
        codec = self.codec()
        for location, bad_versions in (("container", [0, 6, -1, True, 4.0, "4"]),
                                       ("native", [0, 15, -1, True, 14.0, "14"])):
            for version in bad_versions:
                with self.subTest(location=location, version=version):
                    root = generated_project()
                    (root if location == "container" else root["native"])["version"] = version
                    encoded = plistlib.dumps(root, fmt=plistlib.FMT_BINARY)
                    with self.assertRaisesRegex(ValueError, "version"):
                        codec.loads(encoded)
                    with self.assertRaisesRegex(ValueError, "version"):
                        codec.dumps(root)

    def test_rejects_invalid_outer_container_shapes(self):
        codec = self.codec()
        cases = [[], {}, {"version": 4}, generated_project()]
        cases[-1].pop("native")
        for field, value in (("module", b""), ("module", "not bytes"), ("plugins", {}),
                             ("native", []), ("sequence", True), ("sequence", 256)):
            root = generated_project()
            root[field] = value
            cases.append(root)
        for root in cases:
            with self.subTest(root_type=type(root), fields=list(root)[:4]):
                with self.assertRaises(ValueError):
                    codec.dumps(root)
                with self.assertRaises(ValueError):
                    codec.loads(plistlib.dumps(root, fmt=plistlib.FMT_BINARY))

    def test_rejects_wrong_binary_plist_header_and_truncation(self):
        codec = self.codec()
        good = plistlib.dumps(generated_project(), fmt=plistlib.FMT_BINARY)
        for data in (b"", b"bplist00", good[:-10], b"bplist01" + good[8:],
                     b"NOTPLIST" + good[8:], plistlib.dumps(generated_project())):
            with self.subTest(prefix=data[:8]):
                with self.assertRaises(ValueError):
                    codec.loads(data)

    def test_snapshot_framing_rejects_damage_and_version_mismatch(self):
        codec = self.codec()
        payload = snapshot()
        bad = [payload[:10], payload[:-1], payload + b"trailing",
               b"RSONGS3\0" + payload[8:],
               payload[:8] + struct.pack("<III", 0, 1, 1) + b"xx",
               payload[:8] + struct.pack("<III", 1, 1, 0) + b"xx",
               payload[:8] + struct.pack("<III", 0xffffffff, 1, 1) + b"xx",
               b"ordinary module is not an exact snapshot"]
        for value in bad:
            with self.subTest(prefix=value[:20]):
                root = generated_project()
                root["module"] = value
                with self.assertRaisesRegex(ValueError, "snapshot"):
                    codec.loads(plistlib.dumps(root, fmt=plistlib.FMT_BINARY))
                with self.assertRaisesRegex(ValueError, "snapshot"):
                    codec.dumps(root)
        for version in (1, 2, 3):
            root = generated_project()
            root["version"] = version
            with self.subTest(legacy_version=version):
                with self.assertRaisesRegex(ValueError, "snapshot"):
                    codec.dumps(root)

    def test_both_snapshot_versions_preserve_exact_sections(self):
        codec = self.codec()
        self.assertTrue(hasattr(codec, "split_snapshot"), "snapshot splitter not implemented")
        for timed in (False, True):
            root = generated_project()
            root["module"] = snapshot(timed)
            for version in (4, 5):
                root["version"] = version
                if version == 4:
                    root["plugins"][0].pop("instrumentAssignments", None)
                else:
                    root["plugins"][0]["instrumentAssignments"] = []
                decoded = codec.loads(codec.dumps(root))
                self.assertEqual(root, decoded)
                module, samples, timing = codec.split_snapshot(decoded["module"])
                self.assertEqual(module, b"opaque module qualification payload\x00\xff")
                self.assertEqual(samples, bytes(range(256)) + b"\x00\x80\xff\x7f\x00\x00")
                self.assertEqual(timing, b"opaque timing payload\x00" if timed else b"")

    def test_size_limits_apply_to_load_dump_and_snapshot_split(self):
        codec = self.codec()
        root = generated_project()
        data = plistlib.dumps(root, fmt=plistlib.FMT_BINARY, sort_keys=False)
        # Smaller budgets exercise exact boundaries without allocating 600 MiB.
        for name, size in (("MAX_PROJECT_BYTES", len(data)),
                           ("MAX_MODULE_BYTES", len(root["module"]))):
            with self.subTest(limit=name):
                with patch.object(codec, name, size, create=True):
                    self.assertEqual(codec.loads(data), root)
                    self.assertEqual(plistlib.loads(codec.dumps(root)), root)
                with patch.object(codec, name, size - 1, create=True):
                    with self.assertRaisesRegex(ValueError, "size|limit"):
                        codec.loads(data)
                    with self.assertRaisesRegex(ValueError, "size|limit"):
                        codec.dumps(root)
                    if name == "MAX_MODULE_BYTES":
                        with self.assertRaisesRegex(ValueError, "size|limit"):
                            codec.split_snapshot(root["module"])

    def test_rejects_nested_song_snapshot(self):
        codec = self.codec()
        inner = snapshot(False)
        root = generated_project()
        root["module"] = b"RSONGS1\0" + struct.pack("<II", len(inner), 1) + inner + b"x"
        with self.assertRaisesRegex(ValueError, "Nested"):
            codec.dumps(root)

    def test_nested_snapshot_detection_does_not_cross_section_boundary(self):
        codec = self.codec()
        root = generated_project()
        root["module"] = b"RSONGS1\0" + struct.pack("<II", 1, 7) + b"R" + b"SONGS1\0"
        self.assertEqual(root, codec.loads(codec.dumps(root)))

    def test_legacy_containers_are_preserved_not_upgraded(self):
        codec = self.codec()
        for version in (1, 2, 3):
            root = {"version": version, "module": b"opaque legacy module", "plugins": [],
                    "automation": [], "sequence": 0, "unknown": b"preserve"}
            if version == 3:
                native = generated_project()["native"]
                root["native"] = {key: native[key] for key in (
                    "nextID", "patterns", "tracks", "samples", "instruments", "sequences")}
                root["native"]["version"] = 1
            self.assertEqual(root, codec.loads(codec.dumps(root)))

    def test_cli_qualifies_and_repacks_both_extensions_without_overwriting(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR")) as tmp:
            source = Path(tmp) / "generated.resonance"
            output = Path(tmp) / "generated.screamseq"
            root = generated_project()
            original = plistlib.dumps(root, fmt=plistlib.FMT_BINARY)
            source.write_bytes(original)
            command = [sys.executable, "-B", str(PROJECT_DIR / "native_project.py")]
            result = subprocess.run(command + [str(source)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(result.stdout.strip(), "qualification CLI not implemented")
            report = json.loads(result.stdout)
            self.assertEqual(report["container_version"], 5)
            self.assertEqual(report["native_metadata_version"], 14)
            self.assertFalse(report["song_semantics_validated"])
            result = subprocess.run(command + [str(source), "--repack", str(output)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(plistlib.loads(output.read_bytes()), root)
            self.assertEqual(source.read_bytes(), original)
            saved = output.read_bytes()
            for destination in (source, output):
                result = subprocess.run(command + [str(source), "--repack", str(destination)],
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(output.read_bytes(), saved)
            root["native"]["version"] = 15
            source.write_bytes(plistlib.dumps(root, fmt=plistlib.FMT_BINARY))
            rejected_output = Path(tmp) / "must-not-exist.screamseq"
            result = subprocess.run(command + [str(source), "--repack", str(rejected_output)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(rejected_output.exists())


if __name__ == "__main__":
    unittest.main()
