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
(own / "blueprint-recovery.bp.json").write_text(json.dumps(project), encoding="utf-8")
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
