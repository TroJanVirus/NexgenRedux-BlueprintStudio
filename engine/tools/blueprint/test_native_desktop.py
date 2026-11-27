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
                click(1100, 130)
                xdo("type", "--delay", 100, "literal:int")
                key("Return")
                click(1100, 167)

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
print("Real native desktop recovery/save/export/preview/reopen/forced-crash recovery passed.", flush=True)

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
