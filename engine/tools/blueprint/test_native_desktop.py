"""Drive the actual Redux desktop window using X11 input (not TestPlatform).
Run under xvfb-run with xdotool and python3-xlib installed.
"""
import json
import pathlib
import subprocess
import sys
import time

from Xlib import X, display, protocol

binary = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2]).resolve()
own = root / "own"
own.mkdir(parents=True, exist_ok=False)
recovery = own / "blueprint-recovery.bp.json"
project = own / "blueprint-project-Desktop Smoke.bp.json"
asset_root = "bpa-12345-1"
asset = own / asset_root / "Screen" / "asset" / "data.bin"
asset.parent.mkdir(parents=True)
payload = b"desktop\x00asset\xff"
asset.write_bytes(payload)
graphs = []
for name in ("onEnter", "update", "draw", "onSuspend", "onResume", "onExit"):
    graphs.append(dict(owner="Screen", name=name, kind="event",
                       nodes=[dict(id="entry", kind="entry", x=24, y=60)], links=[]))
graphs[0]["nodes"].append(dict(id="save", kind="api:Save:Save::set:string,string",
                              x=360, y=60, values=dict(key="desktop_smoke", value="ok")))
graphs[0]["links"].append(dict(**{"from": "entry"}, to="save", output="then", input="in"))
recovery.write_text(json.dumps(dict(version=2, kind="NativeBlueprint", name="Desktop Draft",
                                    assetRoot=asset_root, graphs=graphs)), encoding="utf-8")


def wait_for(predicate, label, process, timeout=25):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Application exited while waiting for {label}: {process.returncode}")
        if predicate():
            return
        time.sleep(0.1)
    raise RuntimeError(f"Timed out waiting for {label}")


def xdo(*args):
    return subprocess.check_output(["xdotool", *map(str, args)], text=True,
                                   stderr=subprocess.DEVNULL).strip()


def click(x, y):
    xdo("mousemove", x, y)
    time.sleep(0.15)
    xdo("mousedown", 1)
    time.sleep(0.20)
    xdo("mouseup", 1)
    time.sleep(0.35)


def key(name):
    xdo("keydown", name)
    time.sleep(0.20)
    xdo("keyup", name)
    time.sleep(0.35)


def request_close(window_id):
    conn = display.Display()
    try:
        win = conn.create_resource_object("window", window_id)
        msg = protocol.event.ClientMessage(window=win, client_type=conn.intern_atom("WM_PROTOCOLS"),
                                           data=(32, [conn.intern_atom("WM_DELETE_WINDOW"),
                                                      X.CurrentTime, 0, 0, 0]))
        win.send_event(msg, event_mask=0)
        conn.sync()
    finally:
        conn.close()


def exports():
    return sorted(own.glob("bp-*/Screen/screen.json"))


def verify_export(manifest):
    folder = manifest.parent
    assert json.loads(manifest.read_text(encoding="utf-8"))
    assert 'Save::set("desktop_smoke", "ok")' in (folder / "main.as").read_text(encoding="utf-8")
    assert (folder / "asset" / "data.bin").read_bytes() == payload


def run_session(index):
    log = root / f"session-{index}.log"
    with log.open("wb") as out:
        process = subprocess.Popen([str(binary), "--blueprint-studio", "--silent",
                                    "--size", "1280", "720", "--saves", str(root)],
                                   stdout=out, stderr=subprocess.STDOUT)
        window = None
        try:
            def find_window():
                nonlocal window
                try:
                    window = int(xdo("search", "--onlyvisible", "--name", "^Nexgen$").splitlines()[0])
                    return True
                except (subprocess.CalledProcessError, IndexError, ValueError):
                    return False

            wait_for(find_window, "visible native window", process)
            xdo("windowfocus", "--sync", window)
            # Wait for native font and engine initialization before dispatching input.
            time.sleep(2)
            if index == 1:
                click(900, 18)
                xdo("type", "--delay", 100, "Desktop Smoke")
                key("Return")
                wait_for(project.is_file, "saved recovery project", process)
                saved = json.loads(project.read_text(encoding="utf-8"))
                assert saved["name"] == "Desktop Smoke"
                assert saved["graphs"] == graphs
                assert saved["assetRoot"] != asset_root
                assert (own / saved["assetRoot"] / "Screen" / "asset" / "data.bin").read_bytes() == payload
            else:
                click(794, 18)
                click(500, 180)
            before = len(exports())
            click(675, 53)
            wait_for(lambda: len(exports()) > before, "native project export", process)
            for manifest in exports():
                verify_export(manifest)
            click(553, 53)
            wait_for(lambda: "preview started: Screen" in log.read_text(errors="replace"),
                     "preview start", process)
            time.sleep(1)
            key("Escape")
            wait_for(lambda: "preview stopped" in log.read_text(errors="replace"),
                     "preview stop", process)
            assert "Preview error:" not in log.read_text(errors="replace")
            request_close(window)
            assert process.wait(timeout=20) == 0
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            print(log.read_text(errors="replace"), flush=True)


run_session(1)
baseline = project.read_bytes()
run_session(2)
assert project.read_bytes() == baseline
assert asset.read_bytes() == payload
assert len(exports()) == 2
print("Real native desktop recovery/save/export/preview/reopen passed.", flush=True)
