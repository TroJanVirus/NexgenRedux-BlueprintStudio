"""Drive the actual Redux desktop window using X11 input (not TestPlatform).
Run under xvfb-run with xdotool and python3-xlib installed.
"""
import json
import pathlib
import subprocess
import sys
import time

from Xlib import X, display, protocol, error

# Keep an X client connected for the entire suite, including gaps between
# application processes. Do not retry an application that has actually crashed.
def connect_display():
    deadline = time.monotonic() + 15
    while True:
        try:
            connection = display.Display()
            connection.sync()
            return connection
        except (error.DisplayConnectionError, OSError):
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.2)

display_guard = connect_display()

binary = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2]).resolve()
own = root / "own"
own.mkdir(parents=True, exist_ok=False)
recovery = own / "blueprint-recovery.bp.json"
project = own / "blueprint-project-Desktop Smoke.bp.json"
recovered_project = own / "blueprint-project-Desktop Recovered.bp.json"
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
                                   stderr=subprocess.DEVNULL, timeout=10).strip()


def click(x, y, window_id=None):
    if window_id is None:
        xdo("mousemove", x, y)
    else:
        # Studio letterboxes its 1280x720 design in the actual client area.
        geo = display_guard.create_resource_object("window", window_id).get_geometry()
        scale = min(geo.width / 1280, geo.height / 720)
        px = round(x * scale + (geo.width - 1280 * scale) / 2)
        py = round(y * scale + (geo.height - 720 * scale) / 2)
        xdo("mousemove", "--window", window_id, px, py)
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
            # Synchronize on the actual workspace, rather than assuming initialization time.
            wait_for(lambda: "workspace ready" in log.read_text(errors="replace"),
                     "native workspace ready", process)
            time.sleep(0.5)
            if index == 1:
                # A second process using the same own/ directory must fail closed.
                before_recovery = recovery.read_bytes()
                contender = subprocess.run([str(binary), "--blueprint-studio", "--null",
                                            "--frames", "1", "--saves", str(root)],
                                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                           timeout=30, text=True)
                print(contender.stdout, flush=True)
                assert contender.returncode != 0
                assert "save directory is busy or unavailable" in contender.stdout
                assert recovery.read_bytes() == before_recovery
                click(900, 18)
                xdo("type", "--delay", 100, "Desktop Smoke")
                key("Return")
                wait_for(project.is_file, "saved recovery project", process)
                saved = json.loads(project.read_text(encoding="utf-8"))
                assert saved["name"] == "Desktop Smoke"
                assert saved["graphs"] == graphs
                assert saved["assetRoot"] != asset_root
                assert (own / saved["assetRoot"] / "Screen" / "asset" / "data.bin").read_bytes() == payload
            elif index == 2:
                click(794, 18)
                click(500, 180)
                wait_for(lambda: "Opened " + project.name in log.read_text(errors="replace"),
                         "open saved project", process)
            else:
                assert "Recovered unsaved Blueprint" in log.read_text(errors="replace")
                click(900, 18)
                xdo("type", "--delay", 100, "Desktop Recovered")
                key("Return")
                wait_for(recovered_project.is_file, "save crash-recovered project", process)
                saved = json.loads(recovered_project.read_text(encoding="utf-8"))
                assert saved["name"] == "Desktop Recovered"
                assert saved["graphs"] == crashed_graphs
                assert (own / saved["assetRoot"] / "Screen" / "asset" / "data.bin").read_bytes() == payload
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
            if index == 2:
                # Make a real edit and wait for the atomic on-disk checkpoint, then
                # kill the process without invoking any normal shutdown hooks.
                # Non-16:9 client area exercises letterboxed input mapping and resize.
                xdo("windowsize", "--sync", window, 1000, 650)
                wait_for(lambda: (lambda g: (g.width, g.height) == (1000, 650))(
                    display_guard.create_resource_object("window", window).get_geometry()),
                    "resized native window", process)
                time.sleep(0.5)
                click(1100, 130, window)
                xdo("type", "--delay", 100, "literal:int")
                key("Return")
                click(1100, 167, window)

                def checkpoint_has_edit():
                    try:
                        draft = json.loads(recovery.read_text(encoding="utf-8"))
                        nodes = draft["graphs"][0]["nodes"]
                        return len(nodes) == 3 and nodes[-1]["kind"] == "literal:int"
                    except (OSError, ValueError, KeyError, IndexError):
                        return False

                wait_for(checkpoint_has_edit, "on-disk checkpoint for actual graph edit", process)
                draft = json.loads(recovery.read_text(encoding="utf-8"))
                assert draft["graphs"][0]["links"] == graphs[0]["links"]
                # Undo to the exact saved revision and verify the recovery tombstone.
                click(906, 53, window)
                wait_for(lambda: recovery.is_file() and recovery.stat().st_size == 0,
                         "undo clears saved-state checkpoint", process)
                assert project.read_bytes() == baseline
                click(995, 53, window)
                wait_for(checkpoint_has_edit, "redo recreates checkpoint", process)
                assert json.loads(recovery.read_text(encoding="utf-8"))["graphs"] == draft["graphs"]
                process.kill()
                assert process.wait(timeout=20) < 0
                return draft["graphs"]
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
crashed_graphs = run_session(2)
assert project.read_bytes() == baseline
run_session(3)
assert project.read_bytes() == baseline
assert asset.read_bytes() == payload
assert len(exports()) == 3
print("Real native desktop recovery/save/export/preview/reopen/resize/undo/redo/forced-crash recovery passed.", flush=True)

# Exercise the ordinary Redux entry point, not just --blueprint-studio.
menu_root = root / "menu-launch"
menu_root.mkdir()
menu_log = root / "menu-launch.log"
with menu_log.open("wb") as out:
    process = subprocess.Popen([str(binary), "--silent", "--size", "1280", "720",
                                "--saves", str(menu_root)], stdout=out, stderr=subprocess.STDOUT)
    try:
        def menu_window():
            try:
                return int(xdo("search", "--onlyvisible", "--name", "^Nexgen$").splitlines()[0])
            except (subprocess.CalledProcessError, IndexError, ValueError):
                return None

        wait_for(menu_window, "ordinary Redux window", process)
        window = menu_window()
        xdo("windowfocus", "--sync", window)
        wait_for(lambda: "AngelScript" in menu_log.read_text(errors="replace"),
                 "normal engine initialization", process)
        time.sleep(2)

        def launch_from_menu():
            key("Escape")
            key("Down")
            key("Down")
            key("Return")

        def count_launches():
            return menu_log.read_text(errors="replace").count("workspace ready")

        launch_from_menu()
        wait_for(lambda: count_launches() == 1, "Studio opened from Redux menu", process)
        click(1200, 53, window)
        time.sleep(0.5)
        launch_from_menu()
        wait_for(lambda: count_launches() == 2, "Studio reopened from Redux menu", process)
        click(1200, 53, window)
        request_close(window)
        assert process.wait(timeout=20) == 0
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=15)
        print(menu_log.read_text(errors="replace"), flush=True)
print("Ordinary Redux menu launch, return and reopen passed.", flush=True)

# Benchmark the actual editor and GPU with a near-limit graph. Record timings
# rather than claiming a hardware-independent FPS target on shared CI runners.
bench = root / "large-graph"
bench_own = bench / "own"
bench_own.mkdir(parents=True)
large = json.loads(project.read_text(encoding="utf-8"))
large["assetRoot"] = ""
for i in range(1000):
    large["graphs"][0]["nodes"].append(dict(id=f"stress{i}", kind="literal:int",
                                           x=252 * (i % 32), y=120 * (1 + i // 32),
                                           value="42"))
large_path = bench_own / recovery.name
large_path.write_text(json.dumps(large), encoding="utf-8")
bench_log = root / "large-graph.log"
with bench_log.open("wb") as out:
    subprocess.run([str(binary), "--blueprint-studio", "--hidden", "--bench",
                    "--frames", "180", "--size", "1280", "720", "--saves", str(bench)],
                   stdout=out, stderr=subprocess.STDOUT, timeout=90, check=True)
timings = bench_log.read_text(errors="replace")
print(timings, flush=True)
assert "workspace ready (Recovered unsaved Blueprint" in timings
assert "ms a frame:" in timings
assert json.loads(large_path.read_text(encoding="utf-8")) == large
print("Actual desktop renderer benchmark: 1,002 nodes in active graph.", flush=True)
display_guard.close()
