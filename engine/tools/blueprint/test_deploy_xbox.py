import tempfile
import unittest
from pathlib import Path
from deploy_xbox import fatx_component, remote_folder, tree_entries, overlay

class DeploymentTests(unittest.TestCase):
    def test_path_constraints(self):
        self.assertEqual(remote_folder("/E/Games/Preview"), "/E/Games/Preview")
        for path in ("/C/Games/Preview", "/E/Games", "/E/Games/../Other", "/F/Other/Game"):
            with self.assertRaises(ValueError):
                remote_folder(path)
        for name in ("", "..", "bad:name", "a"*43, "trailing."):
            with self.assertRaises(ValueError):
                fatx_component(name)

    def test_overlay_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            src, dst = root/"src", root/"dst"
            src.mkdir()
            dst.mkdir()
            (src/"main.as").write_text("new")
            (dst/"main.as").write_text("old")
            with self.assertRaises(ValueError):
                overlay(src, dst)
            self.assertEqual((dst/"main.as").read_text(), "old")

    def test_tree_rejects_invalid_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root/("a"*43)).write_text("invalid")
            with self.assertRaises(ValueError):
                tree_entries(root)

if __name__ == "__main__":
    unittest.main()
