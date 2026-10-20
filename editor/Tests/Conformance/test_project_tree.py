"""Adversarial comparator cases, separate from native runtime qualification."""
import contextlib
import datetime
import io
import json
from pathlib import Path
import plistlib
import struct
import tempfile
import unittest

import project_tree as tree


class TypedTreeTests(unittest.TestCase):
    def test_bool_integer_real_are_different(self):
        for left, right in ((True, 1), (False, 0), (1, 1.0), (0, -0.0)):
            with self.subTest(left=repr(left), right=repr(right)):
                result = tree.differences({"value": left}, {"value": right})
                self.assertFalse(result["equal"])
                self.assertEqual(result["differences"][0]["kind"], "type")

    def test_integers_never_pass_through_double(self):
        first, second = 2**53, 2**53 + 1
        self.assertEqual(float(first), float(second))
        self.assertNotEqual(tree.digest(first), tree.digest(second))
        for number in (-2**63, 2**63 - 1, 2**64 - 1):
            decoded = plistlib.loads(plistlib.dumps({"number": number}, fmt=plistlib.FMT_BINARY))
            self.assertEqual(tree.typed(decoded["number"]), ["integer", str(number)])

    def test_real_bits_signed_zero_and_nan_payloads(self):
        self.assertFalse(tree.differences(0.0, -0.0)["equal"])
        nan1 = struct.unpack(">d", bytes.fromhex("7ff8000000000001"))[0]
        nan2 = struct.unpack(">d", bytes.fromhex("7ff8000000000002"))[0]
        self.assertTrue(tree.differences(nan1, nan1)["equal"])
        self.assertFalse(tree.differences(nan1, nan2)["equal"])
        value = plistlib.loads(plistlib.dumps({"real": -0.0}, fmt=plistlib.FMT_BINARY))
        self.assertEqual(tree.typed(value["real"]), ["real64", "8000000000000000"])

    def test_dictionary_order_array_order_and_paths(self):
        self.assertTrue(tree.differences({"a": 1, "b": 2}, {"b": 2, "a": 1})["equal"])
        result = tree.differences({"a/b~c": [1, 2]}, {"a/b~c": [2, 1]})
        self.assertEqual([item["path"] for item in result["differences"]], ["/a~1b~0c/0", "/a~1b~0c/1"])
        self.assertFalse(tree.differences([], [None])["equal"])

    def test_binary_unicode_unknown_fields_and_ids_are_not_normalized(self):
        self.assertFalse(tree.differences(b"\x00\xff", b"\x00\xfe")["equal"])
        self.assertFalse(tree.differences(b"abc", "abc")["equal"])
        self.assertFalse(tree.differences("\u00e9", "e\u0301")["equal"])
        for key in ("nextID", "unknownFutureProperty", "instanceID"):
            self.assertFalse(tree.differences({key: 41}, {key: 43})["equal"])

    def test_type_tags_for_null_date_and_uid(self):
        self.assertEqual(tree.typed(None), ["null"])
        self.assertNotEqual(tree.typed(plistlib.UID(1)), tree.typed(1))
        self.assertEqual(tree.typed(datetime.datetime(2026, 10, 9, 1, 2, 3)), ["date-utc", "2026-10-09T01:02:03.000000"])
        with self.assertRaises(tree.EvidenceError):
            tree.typed(datetime.datetime.now(datetime.timezone.utc))

    def test_report_limit_does_not_hide_failure(self):
        result = tree.differences(list(range(10)), list(range(1, 11)), limit=2)
        self.assertFalse(result["equal"])
        self.assertEqual(result["differenceCount"], 10)
        self.assertEqual(len(result["differences"]), 2)
        self.assertTrue(result["truncated"])
        self.assertLess(len(json.dumps(tree.differences("a" * 100000, "b" * 100000))), 1000)

    def test_shared_alias_is_valid_but_cycles_depth_and_foreign_types_fail(self):
        child = [1]
        self.assertTrue(tree.differences([child, child], [[1], [1]])["equal"])
        cyclic = []; cyclic.append(cyclic)
        for value in (cyclic, {1: "invalid key"}, object()):
            with self.assertRaises(tree.EvidenceError):
                tree.typed(value)
        deep = None
        for _ in range(tree.MAX_DEPTH + 2):
            deep = [deep]
        with self.assertRaises(tree.EvidenceError):
            tree.typed(deep)

    def test_strict_read_duplicate_keys_corruption_root_and_size(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "owned.screamseq"
            for contents in (b"not a plist", b"<plist><dict>",
                             b'<plist version="1.0"><dict><key>a</key><integer>1</integer><key>a</key><integer>2</integer></dict></plist>',
                             plistlib.dumps([1, 2], fmt=plistlib.FMT_BINARY)):
                path.write_bytes(contents)
                with self.assertRaises(tree.EvidenceError):
                    tree.read_project(path)
            path.write_bytes(b"12345")
            with self.assertRaises(tree.EvidenceError):
                tree.read_bytes(path, 4)

    def test_cli_exit_status_and_no_input_writes(self):
        with tempfile.TemporaryDirectory() as folder:
            left, right = (Path(folder) / name for name in ("left.screamseq", "right.screamseq"))
            left.write_bytes(plistlib.dumps({"v": 1}, fmt=plistlib.FMT_BINARY))
            right.write_bytes(plistlib.dumps({"v": True}, fmt=plistlib.FMT_BINARY))
            before = left.read_bytes(), right.read_bytes()
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(tree.main(["compare", str(left), str(right)]), 1)
            self.assertFalse(json.loads(output.getvalue())["equal"])
            self.assertEqual((left.read_bytes(), right.read_bytes()), before)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(tree.main(["compare", str(left), str(left)]), 0)

    def test_original_corpus_matches_all_pinned_typed_goldens(self):
        golden = json.loads(Path(__file__).with_name("parity-typed-goldens.json").read_text())
        self.assertEqual(tree.corpus_inventory(tree.CORPUS), golden)
        self.assertEqual(len(golden["projects"]), 5)


if __name__ == "__main__":
    unittest.main()
