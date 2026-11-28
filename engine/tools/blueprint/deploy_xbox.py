#!/usr/bin/env python3
"""Prepare a new OG Xbox Redux development title, optionally upload to a NEW FTP folder."""
import argparse
import ftplib
import getpass
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import sys

INVALID_FATX = set('"+,/:;<=>?[]\\|*')


def fatx_component(name):
    if (not name or name in (".", "..") or len(name) > 42
            or name[-1] in " ."
            or any(ord(c) < 32 or ord(c) > 126 or c in INVALID_FATX for c in name)):
        raise ValueError(f"Not a portable FATX component: {name!r}")
    return name


def tree_entries(root):
    """Validate before copying or uploading; never follow links or case collisions."""
    seen = set()
    entries = []
    for entry in sorted(root.rglob("*")):
        relative = entry.relative_to(root)
        if entry.is_symlink() or not (entry.is_file() or entry.is_dir()):
            raise ValueError(f"Unsupported source entry: {entry}")
        for part in relative.parts:
            fatx_component(part)
        if len(str(relative)) > 220:
            raise ValueError(f"Path too long for Xbox deployment: {relative}")
        key = relative.as_posix().lower()
        if key in seen:
            raise ValueError(f"Case-insensitive collision: {relative}")
        seen.add(key)
        entries.append((entry, relative))
    return entries


def overlay(source, destination):
    """Never silently replace a different runtime file."""
    for entry, relative in tree_entries(source):
        target = destination / relative
        if entry.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                if not target.is_file() or entry.read_bytes() != target.read_bytes():
                    raise ValueError(f"Conflicting runtime/media file: {relative}")
            else:
                shutil.copy2(entry, target)


def prepare(export, runtime, output, cooker, start, development):
    if output.exists():
        raise ValueError("Output must be a new folder.")
    if not export.is_dir() or not runtime.is_dir():
        raise ValueError("Export and runtime must be directories.")
    if not (runtime / "default.xbe").is_file() or not (runtime / "media").is_dir():
        raise ValueError("Select the extracted original Xbox Redux package with default.xbe and media/.")
    for source in (export, runtime):
        if output.resolve().is_relative_to(source.resolve()):
            raise ValueError("Output must be outside both input folders.")
    screens = []
    for directory in sorted(export.iterdir()):
        if directory.name == "_global":
            if not directory.is_dir() or directory.is_symlink():
                raise ValueError("Invalid shared media folder.")
            continue
        if not directory.is_dir() or directory.is_symlink():
            raise ValueError(f"Unexpected export entry: {directory.name}")
        fatx_component(directory.name)
        if not (directory / "main.as").is_file() or not (directory / "screen.json").is_file():
            raise ValueError(f"Incomplete native export: {directory.name}")
        manifest = json.loads((directory / "screen.json").read_text(encoding="utf-8"))
        if not isinstance(manifest, dict):
            raise ValueError(f"Invalid screen manifest: {directory.name}")
        screens.append(directory.name)
    if not screens:
        raise ValueError("No native exported screens.")
    if start not in screens:
        raise ValueError("Choose an exported screen with --start: " + ", ".join(screens))
    fatx_component("screen-" + start)
    tree_entries(export)
    tree_entries(runtime)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".redux-stage-", dir=output.parent) as temporary:
        work = Path(temporary)
        raw, cooked, title = work / "raw", work / "cooked", work / "title"
        (raw / "showcase").mkdir(parents=True)
        for screen in screens:
            shutil.copytree(export / screen, raw / "showcase" / screen)
        if (export / "_global").is_dir():
            shutil.copytree(export / "_global", raw / "global")
        subprocess.run([str(cooker.resolve()), "--platform", "xboxog", str(raw), str(cooked)], check=True)
        shutil.copytree(runtime, title)
        media = title / "media"
        for marker in media.iterdir():
            if marker.is_dir() and (marker.name in ("debug", "dev", "tour", "trial", "demo", "clean", "null") or marker.name.startswith("screen-")):
                if any(marker.iterdir()):
                    raise ValueError(f"Runtime contains a nonempty startup marker: {marker.name}")
                marker.rmdir()
        overlay(cooked, media)
        (media / ("screen-" + start)).mkdir()
        if development:
            (media / "debug").mkdir()
            (media / "dev").mkdir()
        tree_entries(title)
        # The staging folder is on the same filesystem. Never overwrite an
        # existing requested output or reuse an incomplete previous build.
        if output.exists():
            raise ValueError("Output appeared during preparation.")
        title.rename(output)


def remote_folder(path):
    parts = path.strip("/").split("/")
    if len(parts) < 3 or not re.fullmatch(r"[EFGefg]", parts[0]):
        raise ValueError("Use a NEW title directory under /E/Games, /F/Games or /G/Games.")
    if parts[1].lower() != "games":
        raise ValueError("Deployment is restricted to Games directories.")
    for part in parts:
        fatx_component(part)
    return "/" + "/".join(parts)


def upload_new(title, host, port, user, password, destination):
    destination = remote_folder(destination)
    entries = tree_entries(title)
    # FTP is plaintext: explicit user invocation on a trusted LAN only.
    with ftplib.FTP() as ftp:
        ftp.connect(host, port, timeout=15)
        ftp.login(user, password)
        ftp.set_pasv(True)
        # Some dashboard servers treat MKD on an existing folder as success.
        # Probe first and fail closed on ambiguous errors.
        try:
            ftp.cwd(destination)
        except ftplib.error_perm as error:
            if not str(error).startswith("550"):
                raise
        else:
            raise ValueError("Remote title folder already exists; choose a new folder.")
        for _, relative in entries:
            if len(destination + "/" + relative.as_posix()) > 240:
                raise ValueError(f"Remote path is too long: {relative}")
        ftp.mkd(destination)
        try:
            for entry, relative in entries:
                if entry.is_dir():
                    ftp.mkd(destination + "/" + relative.as_posix())
            # Upload the executable last, after media, and verify remote sizes.
            files = [(e, r) for e, r in entries if e.is_file()]
            files.sort(key=lambda pair: pair[1].as_posix().lower() == "default.xbe")
            for entry, relative in files:
                path = destination + "/" + relative.as_posix()
                with entry.open("rb") as stream:
                    ftp.storbinary("STOR " + path, stream)
                ftp.voidcmd("TYPE I")
                if ftp.size(path) != entry.stat().st_size:
                    raise RuntimeError(f"Size verification failed: {relative}")
        except Exception:
            print("Upload incomplete. Do not launch it. No remote files were removed.")
            raise


def run_request(request_file):
    """Studio sends public settings via JSON, FTP password only via stdin."""
    path = Path(request_file)
    result = path.with_name("result.json")
    def report(ok, message):
        fresh = result.with_suffix(".new")
        fresh.write_text(json.dumps({"ok": ok, "message": str(message)[:1000]}), encoding="utf-8")
        fresh.replace(result)
    password = ""
    try:
        options = json.loads(path.read_text(encoding="utf-8"))
        password = sys.stdin.readline(258).rstrip("\r\n")
        host = options.get("host", "")
        destination = options.get("remote", "")
        if host:
            remote_folder(destination)
            if not options.get("user") or not password:
                raise ValueError("FTP user and password are required.")
        prepare(Path(options["export"]), Path(options["runtime"]), Path(options["output"]),
                Path(options["cook"]), options["start"], True)
        if host:
            try:
                upload_new(Path(options["output"]), host, 21, options["user"], password, destination)
            except Exception as error:
                raise RuntimeError("FTP did not complete; do not launch the remote title. " + str(error)) from error
        report(True, "Installed; launch default.xbe from the dashboard." if host else "Cooked title prepared: " + options["output"])
        return 0
    except Exception as error:
        report(False, str(error).replace(password, "[redacted]") if password else str(error))
        return 1


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--request":
        return run_request(sys.argv[2])
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cook", required=True, type=Path)
    parser.add_argument("--start", required=True)
    parser.add_argument("--release", action="store_true", help="Do not enable dev/debug startup markers.")
    parser.add_argument("--ftp-host")
    parser.add_argument("--ftp-port", type=int, default=21)
    parser.add_argument("--ftp-user")
    parser.add_argument("--remote")
    options = parser.parse_args()
    if options.ftp_host and not (options.ftp_user and options.remote):
        parser.error("FTP requires --ftp-user and --remote.")
    if not options.ftp_host and (options.ftp_user or options.remote):
        parser.error("FTP options require --ftp-host.")
    if options.ftp_host:
        remote_folder(options.remote)
    prepare(options.export, options.runtime, options.output, options.cook, options.start, not options.release)
    print(f"Prepared {options.output}; real Xbox compatibility is unverified.")
    if options.ftp_host:
        password = getpass.getpass("FTP password (trusted LAN only): ")
        upload_new(options.output, options.ftp_host, options.ftp_port, options.ftp_user, password, options.remote)
        print("FTP upload complete (file sizes checked). Launch default.xbe manually from the dashboard.")


if __name__ == "__main__":
    sys.exit(main())
