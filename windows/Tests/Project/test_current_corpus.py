"""Current Mac-produced files through the legacy framing tool, without an app."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / "windows/Project"), str(ROOT / "editor/Tests/Conformance")]
import native_project
from project_tree import CORPUS, corpus_inventory, differences, read_bytes


class CurrentCorpusTests(unittest.TestCase):
    def test_all_original_current_files_preserve_typed_tree_and_payloads(self):
        inventory = corpus_inventory(CORPUS)
        self.assertEqual(len(inventory["projects"]), 5)
        for relative in inventory["projects"]:
            with self.subTest(project=relative):
                path = CORPUS / relative
                original = read_bytes(path)
                first = native_project.loads(original)
                self.assertEqual((first["version"], first["native"]["version"]), (6, 17))
                second = native_project.loads(native_project.dumps(native_project.loads(original)))
                self.assertTrue(differences(first, second)["equal"])
                self.assertEqual(first["module"], second["module"])
                self.assertEqual(read_bytes(path), original)


if __name__ == "__main__":
    unittest.main()
