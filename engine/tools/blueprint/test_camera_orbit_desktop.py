"""Open and play the editable orbit Blueprint on the real X11 renderer.
Screenshots are native window captures, not reconstructed images.
"""
import json
import pathlib
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
    click(x, y)
    xdo("type", "--delay", 35, text)
    xdo("key", "Return")
    time.sleep(0.3)

def wire(x1, y1, x2, y2):
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
                "make:Vec3": {"x": "0", "y": "0.5", "z": "0"}}
    for n in actual["nodes"]:
        if n["kind"] in required:
            assert all(n.get("values", {}).get(k) == v for k, v in required[n["kind"]].items()), n

def author_scene(process):
    # Blank native document; every node is inserted via the right-click palette,
    # every value is entered through its actual input pin and every wire is dragged.
    right_add(500, 100, "spawn:plane")
    right_add(760, 100, "spawn:box")
    value(760, 160, "Cube")
    wire(468, 138, 500, 138)
    wire(696, 138, 760, 138)
    assert_authored(process, 0, 3, 2)
    capture("01-on-enter")
    click(100, 122)
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
    print("GUI-authored camera orbit: 11 active nodes, 10 links, exported native source.", flush=True)

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

with log.open("wb") as out:
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
        click(553, 53)
        wait_for(lambda: "preview started: Screen" in log.read_text(errors="replace"), "orbit preview", process)
        start = time.monotonic()
        for seconds, label in [(0.1,"04-start"),(2,"05-quarter"),(4,"06-half"),(6,"07-three-quarters"),(9,"08-return"),(10,"09-hold")]:
            time.sleep(max(0, start + seconds - time.monotonic()))
            assert process.poll() is None
            capture(label)
        xdo("key", "Escape")
        wait_for(lambda: "preview stopped" in log.read_text(errors="replace"), "preview stop", process)
        assert "Preview error:" not in log.read_text(errors="replace")
        print("Native orbit Blueprint loaded, rendered and captured at six times.")
    finally:
        process.terminate()
        process.wait(timeout=10)
        conn.close()
        print(log.read_text(errors="replace"))
