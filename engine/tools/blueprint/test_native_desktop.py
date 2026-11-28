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


def click(x, y, window_id=None, button=1):
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
    xdo("mousedown", button)
    time.sleep(0.20)
    xdo("mouseup", button)
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
                click(850, 18)
                click(755, 133)
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
                click(850, 18)
                click(755, 133)
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

        # Author a fresh graph entirely through the real native UI, rather than
        # starting with serialized fixture nodes. These coordinates are shared
        # with the runtime workspace authoring regression.
        def enter_text(value):
            xdo("type", "--delay", 65, value)
            key("Return")

        click(1100, 130, window)
        enter_text("api:Save:Save::set:string,string")
        click(1100, 167, window)
        click(468, 138, window)
        click(630, 353, window)
        click(630, 375, window)
        enter_text("native_authoring")
        click(630, 397, window)
        enter_text("first")
        click(850, 18, window)
        click(755, 133, window)
        enter_text("Authored Desktop")
        authored_own = menu_root / "own"
        authored = authored_own / "blueprint-project-Authored Desktop.bp.json"
        wait_for(authored.is_file, "new graph saved through desktop UI", process)
        data = json.loads(authored.read_text(encoding="utf-8"))
        graph = data["graphs"][0]
        assert len(graph["nodes"]) == 2 and len(graph["links"]) == 1
        action = graph["nodes"][1]
        assert action["kind"] == "api:Save:Save::set:string,string"
        assert action["values"] == dict(key="native_authoring", value="first")
        assert graph["links"][0] == dict(**{"from": "entry"}, to=action["id"],
                                         output="then", input="in")

        # Selected node inputs can be edited without leaving the graph.
        # Existing socket clicks still invoke the legacy typed editor.
        click(630, 397, window)
        key("Escape")
        click(755, 397, window)
        enter_text("inline")
        wait_for(lambda: "inline input applied: value" in menu_log.read_text(errors="replace"),
                 "inline scalar edit", process)
        click(800, 53, window)
        wait_for(lambda: json.loads(authored.read_text())["graphs"][0]["nodes"][1]["values"]["value"].replace("inline", "", 1) == "first" and "inline" in json.loads(authored.read_text())["graphs"][0]["nodes"][1]["values"]["value"],
                 "inline typing preserves existing characters", process)
        key("ctrl+z")
        click(800, 53, window)
        wait_for(lambda: json.loads(authored.read_text())["graphs"][0]["nodes"][1]["values"]["value"] == "first",
                 "inline edit undone", process)

        # Delete is a graph shortcut, with Cancel and one-step undo.
        click(650, 330, window)
        key("Delete")
        key("Escape")
        click(800, 53, window)
        assert len(json.loads(authored.read_text())["graphs"][0]["nodes"]) == 2
        key("Delete")
        key("Return")
        click(800, 53, window)
        wait_for(lambda: len(json.loads(authored.read_text())["graphs"][0]["nodes"]) == 1,
                 "keyboard node deletion", process)
        key("ctrl+z")
        click(800, 53, window)
        wait_for(lambda: len(json.loads(authored.read_text())["graphs"][0]["nodes"]) == 2,
                 "keyboard deletion undone", process)

        # Right-click a data socket and promote it through the native pin menu.
        click(630, 375, window, button=3)
        click(700, 375 + 30 + 3 * 30 + 12, window)
        enter_text("promotedKey")
        click(800, 53, window)
        wait_for(lambda: len(json.loads(authored.read_text())["graphs"][0]["nodes"]) == 3,
                 "native pin promotion", process)
        key("ctrl+z")
        click(800, 53, window)
        wait_for(lambda: len(json.loads(authored.read_text())["graphs"][0]["nodes"]) == 2,
                 "pin promotion undone", process)

        # An external valid file modification must not be overwritten, even
        # if the current authoring process still holds the cooperative lease.
        external = authored.read_bytes() + b"\n"
        authored.write_bytes(external)
        click(630, 397, window)
        for _ in range(5):
            key("BackSpace")
        enter_text("second")
        click(800, 53, window)
        wait_for(lambda: "File changed outside Studio" in menu_log.read_text(errors="replace"),
                 "save conflict rejected", process)
        assert authored.read_bytes() == external
        key("Escape")  # Dismiss the save-conflict notice before opening Projects.
        click(850, 18, window)
        click(755, 133, window)
        enter_text("Authored Copy")
        copy = authored_own / "blueprint-project-Authored Copy.bp.json"
        wait_for(copy.is_file, "save as preserves authored changes", process)
        copy_data = json.loads(copy.read_text(encoding="utf-8"))
        assert copy_data["graphs"][0]["nodes"][1]["values"]["value"] == "second"
        assert authored.read_bytes() == external

        click(675, 53, window)
        def authored_exports():
            return list(authored_own.glob("bp-*/Screen/screen.json"))
        wait_for(authored_exports, "export freshly authored graph", process)
        generated = authored_exports()[0].with_name("main.as").read_text(encoding="utf-8")
        assert 'Save::set("native_authoring", "second")' in generated
        click(553, 53, window)
        wait_for(lambda: "preview started: Screen" in menu_log.read_text(errors="replace"),
                 "playtest freshly authored graph", process)
        key("Escape")
        wait_for(lambda: "preview stopped" in menu_log.read_text(errors="replace"),
                 "stop freshly authored graph", process)
        assert "Preview error:" not in menu_log.read_text(errors="replace")
        # Author reusable assets through the native Content Browser and persist
        # a native UI page without touching the previously generated graph.
        click(1027, 18, window)
        click(454, 133, window)
        click(528, 172, window)
        click(810, 249, window)
        enter_text("Health")
        click(720, 293, window)
        click(910, 293, window)
        click(720, 334, window)
        # Open native UI Designer: add a container, nest a label, edit its
        # width, and switch the native-rendered preview to 4:3 and back.
        click(445, 564, window)
        click(445, 172, window)
        click(445, 220, window)
        click(445, 380, window)
        click(445, 172, window)
        click(445, 420, window)
        click(445, 420, window)
        click(810, 455, window)
        enter_text("320")
        click(910, 172, window)
        click(910, 172, window)
        click(720, 172, window)
        click(720, 564, window)
        click(910, 564, window)
        click(360, 172, window)
        # New actor is selected. Add a child, select it and change its
        # shape, then open the actual native component inspector.
        click(815, 521, window)
        click(815, 414, window)
        click(910, 293, window)
        click(815, 249, window)
        click(720, 293, window)
        for _ in range(len("Component1")):
            key("BackSpace")
        enter_text("Body")
        click(815, 574, window)
        click(720, 293, window)
        for _ in range(len("none")):
            key("BackSpace")
        enter_text("sphere:0.5")
        click(910, 293, window)
        enter_text("2")
        click(815, 249, window)
        click(815, 564, window)
        # Drag the actor from the content list onto the plan, then move it.
        click(445, 564, window)
        def drag_plan(x1, y1, x2, y2):
            geo = display_guard.create_resource_object("window", window).get_geometry()
            scale = min(geo.width / 1280, geo.height / 720)
            def move(x, y):
                xdo("mousemove", "--window", window,
                    round(x * scale + (geo.width - 1280 * scale) / 2),
                    round(y * scale + (geo.height - 720 * scale) / 2))
                time.sleep(0.25)
            move(x1, y1)
            xdo("mousedown", 1)
            time.sleep(0.2)
            move(x2, y2)
            xdo("mouseup", 1)
            time.sleep(0.35)
        drag_plan(440, 250, 857, 428)
        drag_plan(857, 428, 885, 442)
        click(930, 133, window)
        click(800, 53, window)
        def saved_library():
            try:
                return json.loads(copy.read_text(encoding="utf-8")).get("visualAssets", [])
            except (OSError, ValueError):
                return []
        wait_for(lambda: len(saved_library()) == 2, "native visual library save", process)
        library = saved_library()
        widget = next(a for a in library if a["kind"] == "widget")
        actor = next(a for a in library if a["kind"] == "actor")
        page = json.loads(widget["source"])
        assert page[0]["label"] == "Health" and page[1]["button"] == "Button"
        assert page[2]["progress"] == 0.75
        assert page[3]["column"][0]["label"] == "Text"
        assert page[3]["column"][0]["width"] == 320
        actor_data = json.loads(actor["source"])
        assert actor_data["parents"] == [-1, 0]
        assert actor_data["entities"][0]["shape"] == "box"
        child = actor_data["entities"][1]
        assert child["name"] == "Body" and child["shape"] == "sphere"
        assert child["sphere"] == 0.5 and child["mass"] == 2
        saved_root = json.loads(copy.read_text(encoding="utf-8"))["assetRoot"]
        published = authored_own / saved_root / "Screen" / "asset" / ("ui_" + widget["id"] + ".json")
        assert published.read_text(encoding="utf-8") == widget["source"]
        linked_graphs = json.loads(copy.read_text(encoding="utf-8"))["graphs"]
        assert any(n["kind"] == "ui:load-asset" for n in linked_graphs[0]["nodes"])
        placements = [n for n in linked_graphs[0]["nodes"]
                      if n["kind"] == "actor:spawn-asset" and n["values"]["id"] == actor["id"]]
        assert len(placements) == 2
        assert [float(v) for v in placements[-1]["values"]["place"].split(",")] == [5, 0, 3]
        assert any(n["kind"] == "api:Ui:Ui::draw:Canvas@" for n in linked_graphs[2]["nodes"])
        # The content drawer must create a real native UI thumbnail from this saved widget.
        click(1200, 688, window)
        wait_for(lambda: "native widget thumbnail ready: " + widget["id"] in menu_log.read_text(errors="replace"),
                 "native widget thumbnail", process)
        click(1200, 688, window)
        before_preview = menu_log.read_text(errors="replace").count("preview started: Screen")
        click(553, 53, window)
        wait_for(lambda: menu_log.read_text(errors="replace").count("preview started: Screen") > before_preview, "play linked widget page", process)
        time.sleep(1)
        key("Escape")
        assert "Preview error:" not in menu_log.read_text(errors="replace")
        print("Native Content Browser: actor hierarchy, physics inspector, linked actor and UI authoring passed.", flush=True)
        click(1200, 53, window)
        request_close(window)
        assert process.wait(timeout=20) == 0
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=15)
        print(menu_log.read_text(errors="replace"), flush=True)
print("Ordinary Redux menu launch, return, reopen, fresh graph authoring, pin wiring, save conflict, Save As, export and play passed.", flush=True)

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
