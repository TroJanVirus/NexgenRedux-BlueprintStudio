import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch, MagicMock
import io
import json
import shutil
from deploy_xbox import fatx_component, remote_folder, tree_entries, overlay, prepare, run_request, upload_new, plan_update, upload_update, IDENTITY_FILES

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

    def test_prepare_development_title(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            export, runtime, output = root/"export", root/"runtime", root/"title"
            (export/"Screen").mkdir(parents=True)
            (export/"Screen"/"main.as").write_text("class Example {}")
            (export/"Screen"/"screen.json").write_text("{}")
            (export/"_global").mkdir()
            (export/"_global"/"project.txt").write_text("shared")
            (runtime/"media"/"showcase").mkdir(parents=True)
            (runtime/"default.xbe").write_bytes(b"fixture")
            (runtime/"media"/"tour").mkdir()
            def fake_cook(command, check):
                self.assertEqual(command[1:3], ["--platform", "xboxog"])
                self.assertTrue(check)
                shutil.copytree(command[3], command[4])
            with patch("deploy_xbox.subprocess.run", side_effect=fake_cook):
                prepare(export, runtime, output, root/"cook", "Screen", True)
                self.assertTrue((output/"media"/"screen-Screen").is_dir())
                self.assertTrue((output/"media"/"debug").is_dir())
                self.assertTrue((output/"media"/"dev").is_dir())
                self.assertFalse((output/"media"/"tour").exists())
                self.assertEqual((output/"media"/"showcase"/"Screen"/"main.as").read_text(), "class Example {}")
                self.assertEqual((output/"media"/"global"/"project.txt").read_text(), "shared")
                self.assertTrue((runtime/"media"/"tour").is_dir())
                with self.assertRaises(ValueError):
                    prepare(export, runtime, output, root/"cook", "Screen", True)

    def test_native_request_password_not_persisted(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            request = root/"request.json"
            settings = dict(export="export", runtime="runtime", output="output",
                            cook="cook", start="Screen", host="192.168.1.50",
                            remote="/E/Games/Test", user="xbox")
            request.write_text(json.dumps(settings))
            with patch("sys.stdin", io.StringIO("sample-secret\n")), \
                 patch("deploy_xbox.prepare") as stage, \
                 patch("deploy_xbox.upload_new") as upload:
                self.assertEqual(run_request(request), 0)
                stage.assert_called_once()
                upload.assert_called_once_with(Path("output"), "192.168.1.50", 21,
                                               "xbox", "sample-secret", "/E/Games/Test")
            result = root/"result.json"
            self.assertTrue(json.loads(result.read_text())["ok"])
            self.assertNotIn("sample-secret", result.read_text()+request.read_text())
            with patch("sys.stdin", io.StringIO("sample-secret\n")), \
                 patch("deploy_xbox.prepare"), \
                 patch("deploy_xbox.upload_new", side_effect=RuntimeError("sample-secret")):
                self.assertEqual(run_request(request), 1)
            self.assertFalse(json.loads(result.read_text())["ok"])
            self.assertNotIn("sample-secret", result.read_text())

    def test_ftp_refuses_existing_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            title=Path(temp)
            (title/"default.xbe").write_bytes(b"fixture")
            connection=MagicMock()
            connection.__enter__.return_value=connection
            with patch("deploy_xbox.ftplib.FTP", return_value=connection):
                with self.assertRaisesRegex(ValueError, "already exists"):
                    upload_new(title,"192.168.1.50",21,"xbox","test","/E/Games/Test")
            connection.mkd.assert_not_called()
            connection.storbinary.assert_not_called()

    def test_incremental_plan_and_verified_transfer(self):
        with tempfile.TemporaryDirectory() as temp:
            previous, fresh = Path(temp)/"old", Path(temp)/"new"
            for root, code in ((previous,"old"), (fresh,"new")):
                (root/"media/showcase/Screen").mkdir(parents=True)
                (root/"default.xbe").write_bytes(b"same-runtime")
                (root/"media/showcase/Screen/main.as").write_text(code)
                for name in IDENTITY_FILES:
                    (root/name).write_text(("a" if root == previous else "b")*32)
            self.assertEqual([c[0] for c in plan_update(previous,fresh)],
                             ["media/showcase/Screen/main.as"])
            destination = "/E/Games/Test"
            files = {destination+"/"+p.relative_to(previous).as_posix():p.read_bytes()
                     for p in previous.rglob("*") if p.is_file()}
            connection=MagicMock()
            connection.__enter__.return_value=connection
            connection.retrbinary.side_effect=lambda command, callback: callback(files[command[5:]])
            connection.storbinary.side_effect=lambda command, stream: files.__setitem__(command[5:],stream.read())
            def rename(source,target):
                assert target not in files
                files[target]=files.pop(source)
            connection.rename.side_effect=rename
            connection.delete.side_effect=lambda path: files.pop(path)
            with patch("deploy_xbox.ftplib.FTP", return_value=connection):
                self.assertEqual(upload_update(previous,fresh,"192.168.1.50",21,"xbox","test",destination),1)
            self.assertEqual(files[destination+"/media/showcase/Screen/main.as"],b"new")
            self.assertEqual(connection.storbinary.call_count,1)
            self.assertFalse(any(".bak" in p or ".tmp" in p for p in files))
            self.assertEqual((fresh/IDENTITY_FILES[0]).read_text(),"a"*32)
            (fresh/"default.xbe").write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError,"Runtime"):
                plan_update(previous,fresh)
            (fresh/"default.xbe").write_bytes(b"same-runtime")
            (fresh/"media/showcase/Screen/new.as").write_text("added")
            with self.assertRaisesRegex(ValueError,"added/removed"):
                plan_update(previous,fresh)

if __name__ == "__main__":
    unittest.main()
