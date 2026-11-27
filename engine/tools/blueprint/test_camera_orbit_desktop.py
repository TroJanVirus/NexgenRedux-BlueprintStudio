"""Open and play the editable orbit Blueprint on the real X11 renderer.
Screenshots are native window captures, not reconstructed images.
"""
import json
import os
import pathlib
import signal
import struct
import subprocess
import sys
import time
import zlib
from Xlib import X, display

binary = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2]).resolve()
own = root / "own"
own.mkdir(parents=True)
fixture = pathlib.Path(__file__).parent / "examples" / (sys.argv[3] if len(sys.argv) > 3 else "camera-orbit.bp.json")
project = json.loads(fixture.read_text(encoding="utf-8"))
author = "--author" in sys.argv
record = "--record" in sys.argv
timed = fixture.name == "camera-orbit-timed.bp.json"
operations = {"insert": 0, "wire": 0, "value": 0, "graph_switch": 0}
if timed and author:
    project["graphs"][0]["nodes"][2]["values"]["place"] = "0,0.5,0"
    project["graphs"][0]["nodes"][2]["values"]["color"] = "0.92,0.35,0.12,1"
recovery = own / "blueprint-recovery.bp.json"
if not author:
    recovery.write_text(json.dumps(project), encoding="utf-8")
conn = display.Display()
log = root / "orbit.log"

def xdo(*args):
    return subprocess.check_output(["xdotool", *map(str, args)], text=True, stderr=subprocess.DEVNULL, timeout=10).strip()

def wait_for(predicate, label, process, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"App exited while waiting for {label}: {process.returncode}")
        if predicate():
            return
        time.sleep(0.1)
    raise RuntimeError(f"Timed out: {label}")

def click(x, y):
    xdo("mousemove", "--window", window, x, y)
    time.sleep(0.12)
    xdo("mousedown", 1)
    time.sleep(0.15)
    xdo("mouseup", 1)
    time.sleep(0.2)

def right_add(x, y, query):
    operations["insert"] += 1
    xdo("mousemove", "--window", window, x, y)
    time.sleep(0.12)
    xdo("mousedown", 3)
    time.sleep(0.15)
    xdo("mouseup", 3)
    time.sleep(0.2)
    xdo("type", "--delay", 35, query)
    time.sleep(0.2)
    xdo("key", "Return")
    time.sleep(0.3)

def value(x, y, text):
    operations["value"] += 1
    click(x, y)
    xdo("type", "--delay", 35, text)
    xdo("key", "Return")
    time.sleep(0.3)

def components(x, y, values):
    operations["value"] += 1
    click(x, y)
    for i, text in enumerate(values):
        click(int(360 + 560 / len(values) * (i + 0.5)), 317)
        xdo("type", "--delay", 35, text)
    xdo("key", "Return")
    time.sleep(0.3)

def wire(x1, y1, x2, y2):
    operations["wire"] += 1
    xdo("mousemove", "--window", window, x1, y1)
    time.sleep(0.12)
    xdo("mousedown", 1)
    time.sleep(0.16)
    xdo("mousemove", "--window", window, x2, y2)
    time.sleep(0.18)
    xdo("mouseup", 1)
    time.sleep(0.28)

def assert_authored(process, graph_index, nodes, links):
    def has_graph():
        try:
            graph = json.loads(recovery.read_text(encoding="utf-8"))["graphs"][graph_index]
            return len(graph["nodes"]) == nodes and len(graph["links"]) == links
        except (OSError, ValueError, KeyError, IndexError):
            return False
    wait_for(has_graph, f"graph {graph_index}: {nodes} nodes, {links} wires", process)
    actual = json.loads(recovery.read_text(encoding="utf-8"))["graphs"][graph_index]
    expected = project["graphs"][graph_index]
    def signature(g):
        kinds = {n["id"]: n["kind"] for n in g["nodes"]}
        return (sorted(kinds.values()), sorted((kinds[w["from"]], w["output"], kinds[w["to"]], w["input"]) for w in g["links"]))
    assert signature(actual) == signature(expected), (actual, expected)
    # Explicit scalar settings must survive checkpoint. Other values are node defaults.
    for n in actual["nodes"]:
        ref = next(e for e in expected["nodes"] if e["kind"] == n["kind"])
        for key, val in n.get("values", {}).items():
            assert ref.get("values", {}).get(key) == val, (key, val, ref)
    required = {"spawn:box": {"name": "Cube"}, "time:timeline": {"duration": "8"},
                "make:Vec3": {"x": "0", "y": "0.5", "z": "0"},
                "camera:orbit-timed": {"name": "Cube", "duration": "8"}}
    if timed and graph_index == 0:
        required["spawn:box"].update({"place": "0,0.5,0", "color": "0.92,0.35,0.12,1"})
    for n in actual["nodes"]:
        if n["kind"] in required:
            assert all(n.get("values", {}).get(k) == v for k, v in required[n["kind"]].items()), n

def author_scene(process):
    # Blank native document; every node is inserted via the right-click palette,
    # every value is entered through its actual input pin and every wire is dragged.
    right_add(500, 100, "spawn:plane")
    right_add(760, 100, "spawn:box")
    value(760, 160, "Cube")
    if timed:
        components(760, 182, ["0", "0.5", "0"])
        components(760, 226, ["0.92", "0.35", "0.12", "1"])
    wire(468, 138, 500, 138)
    wire(696, 138, 760, 138)
    assert_authored(process, 0, 3, 2)
    capture("01-on-enter")
    operations["graph_switch"] += 1
    click(100, 122)
    if timed:
        right_add(500, 100, "camera:orbit-timed")
        operations["value"] += 1
        click(500, 160)
        click(500, 485)  # Cube sorts before Floor in the known-target list.
        xdo("key", "Return")
        time.sleep(0.3)
        value(500, 226, "8")
        wire(468, 138, 500, 138)
        assert_authored(process, 1, 2, 1)
        capture("02-update")
        operations["graph_switch"] += 1
        click(100, 153)
        right_add(500, 100, "render:space")
        wire(468, 138, 500, 138)
        assert_authored(process, 2, 2, 1)
        capture("03-draw")
    else:
        right_add(500, 100, "time:timeline")
        right_add(272, 330, "make:Vec3")
        right_add(500, 330, "math:orbit-position")
        right_add(760, 100, "api:Space:Space::camera:Vec3,Vec3,float")
        value(500, 182, "8")
        value(272, 368, "0")
        value(272, 390, "0.5")
        value(272, 412, "0")
        wire(468, 138, 500, 138)
        wire(696, 138, 760, 138)
        wire(696, 160, 500, 434)
        wire(468, 368, 500, 368)
        wire(468, 368, 760, 182)
        wire(696, 368, 760, 160)
        assert_authored(process, 1, 5, 6)
        capture("02-update")
        operations["graph_switch"] += 1
        click(100, 153)
        right_add(272, 250, "event:canvas")
        right_add(500, 100, "api:Space:Space::draw:Canvas@")
        wire(468, 138, 500, 138)
        wire(468, 288, 500, 160)
        assert_authored(process, 2, 3, 2)
        capture("03-draw")
    # Retain the actual GUI-authored editable project, before launch.
    (root / "gui-authored-camera-orbit.bp.json").write_bytes(recovery.read_bytes())
    click(675, 53)
    wait_for(lambda: bool(list(own.glob("bp-*/Screen/main.as"))), "native export", process)
    exported = sorted(own.glob("bp-*/Screen/main.as"))[-1].read_text(encoding="utf-8")
    assert "SolidBox" in exported and "SolidPlane" in exported and "Space::camera" in exported
    (root / "gui-authored-main.as").write_text(exported, encoding="utf-8")
    expected = {"insert": 4 if timed else 8, "wire": 4 if timed else 10, "value": 5, "graph_switch": 2}
    assert operations == expected, (operations, expected)
    (root / "authoring-operations.json").write_text(json.dumps(operations, indent=2), encoding="utf-8")
    print(f"GUI-authored camera orbit: {operations}, exported native source.", flush=True)

def capture(label):
    win = conn.create_resource_object("window", window)
    geo = win.get_geometry()
    image = win.get_image(0, 0, geo.width, geo.height, X.ZPixmap, 0xffffffff)
    data = image.data
    # Xvfb is configured for depth 24, stored as little-endian BGRX.
    assert len(data) == geo.width * geo.height * 4
    scanlines = []
    for y in range(geo.height):
        row = data[y * geo.width * 4:(y + 1) * geo.width * 4]
        rgb = bytearray(geo.width * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = row[2::4], row[1::4], row[0::4]
        scanlines.append(b"\x00" + rgb)
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", geo.width, geo.height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(scanlines))) + chunk(b"IEND", b"")
    (root / (label + ".png")).write_bytes(png)
    if label[0] == "0" and label[1] in "456789":
        orange = 0
        for yy in range(geo.height // 3, geo.height * 2 // 3):
            for xx in range(geo.width // 3, geo.width * 2 // 3):
                b, g, r = data[(yy * geo.width + xx) * 4:(yy * geo.width + xx) * 4 + 3]
                if r > 70 and r > g * 1.3 and g > b * 1.25:
                    orange += 1
        print(f"{label}: central orange pixels={orange}", flush=True)
        if label != "04-start" and orange < 100:
            raise RuntimeError(f"{label}: cube color not visible in central third")

with log.open("wb") as out:
    recorder = None
    recorder_log = None
    process = subprocess.Popen([str(binary), "--blueprint-studio", "--silent", "--size", "1280", "720", "--saves", str(root)], stdout=out, stderr=subprocess.STDOUT)
    try:
        window = None
        def find_window():
            global window
            try:
                window = int(xdo("search", "--onlyvisible", "--name", "^Nexgen$").splitlines()[0])
                return True
            except (subprocess.CalledProcessError, IndexError, ValueError):
                return False
        wait_for(find_window, "visible window", process)
        xdo("windowfocus", "--sync", window)
        wait_for(lambda: "workspace ready" in log.read_text(errors="replace"), "studio", process)
        time.sleep(0.6)
        if author:
            author_scene(process)
        else:
            capture("01-on-enter")
            click(100, 122)
            capture("02-update")
            click(100, 153)
            capture("03-draw")
        if record:
            recorder_log = (root / "recording.log").open("wb")
            recorder = subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "warning", "-nostdin", "-y",
                "-f", "x11grab", "-framerate", "15", "-window_id", str(window), "-i", os.environ["DISPLAY"],
                "-an", "-t", "12", "-c:v", "libx264", "-threads", "2", "-preset", "ultrafast", "-crf", "23",
                "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(root / "native-preview.mp4")],
                stdout=recorder_log, stderr=subprocess.STDOUT)
            time.sleep(0.4)
            if recorder.poll() is not None:
                raise RuntimeError("Native recording could not start")
        click(553, 53)
        wait_for(lambda: "preview started: Screen" in log.read_text(errors="replace"), "orbit preview", process)
        start = time.monotonic()
        for seconds, label in [(0.1,"04-start"),(2,"05-quarter"),(4,"06-half"),(6,"07-three-quarters"),(9,"08-return"),(10,"09-hold")]:
            time.sleep(max(0, start + seconds - time.monotonic()))
            assert process.poll() is None
            capture(label)
        if recorder is not None:
            assert recorder.wait(timeout=20) == 0, "Native recording failed"
            assert (root / "native-preview.mp4").stat().st_size > 10000
            print("Retained native preview recording.", flush=True)
        xdo("key", "Escape")
        wait_for(lambda: "preview stopped" in log.read_text(errors="replace"), "preview stop", process)
        assert "Preview error:" not in log.read_text(errors="replace")
        print("Native orbit Blueprint loaded, rendered and captured at six times.")
    finally:
        if recorder is not None and recorder.poll() is None:
            recorder.send_signal(signal.SIGINT)
            try:
                recorder.wait(timeout=10)
            except subprocess.TimeoutExpired:
                recorder.kill()
                recorder.wait()
        if recorder_log is not None:
            recorder_log.close()
        process.terminate()
        process.wait(timeout=10)
        conn.close()
        print(log.read_text(errors="replace"))
