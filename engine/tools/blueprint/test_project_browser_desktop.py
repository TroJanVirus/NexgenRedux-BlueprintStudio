"""Native X11 project-library and first-person starter regression."""
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
root.mkdir(parents=True)
own = root / "own"
log = root / "library.log"
conn = display.Display()

def xdo(*args):
    return subprocess.check_output(["xdotool", *map(str,args)], text=True, stderr=subprocess.DEVNULL, timeout=10).strip()

def wait_for(predicate, label, timeout=45):
    until = time.monotonic()+timeout
    while time.monotonic()<until:
        if process.poll() is not None:
            raise RuntimeError(f"Redux exited during {label}: {process.returncode}")
        if predicate():
            return
        time.sleep(0.1)
    raise RuntimeError(f"Timed out: {label}")

def find_window():
    global window
    try:
        window = int(xdo("search","--onlyvisible","--name","^Nexgen$").splitlines()[0])
        return True
    except (subprocess.CalledProcessError, IndexError, ValueError):
        return False

def logged(message):
    return message in log.read_text(errors="replace")

def click(x,y):
    xdo("mousemove","--window",window,x,y)
    time.sleep(0.15)
    xdo("mousedown",1)
    time.sleep(0.2)
    xdo("mouseup",1)
    time.sleep(0.3)

def key(name, duration=0.2):
    xdo("keydown",name)
    try:
        time.sleep(duration)
    finally:
        xdo("keyup",name)
    time.sleep(0.35)

def capture(name):
    win=conn.create_resource_object("window",window)
    geo=win.get_geometry()
    image=win.get_image(0,0,geo.width,geo.height,X.ZPixmap,0xffffffff)
    data=image.data
    assert len(data)==geo.width*geo.height*4
    scanlines=[]
    for yy in range(geo.height):
        row=data[yy*geo.width*4:(yy+1)*geo.width*4]
        rgb=bytearray(geo.width*3)
        rgb[0::3],rgb[1::3],rgb[2::3]=row[2::4],row[1::4],row[0::4]
        scanlines.append(b"\x00"+rgb)
    def chunk(kind,payload):
        return struct.pack(">I",len(payload))+kind+payload+struct.pack(">I",zlib.crc32(kind+payload)&0xffffffff)
    png=b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",geo.width,geo.height,8,2,0,0,0))
    png+=chunk(b"IDAT",zlib.compress(b"".join(scanlines)))+chunk(b"IEND",b"")
    (root/(name+".png")).write_bytes(png)
    return zlib.crc32(data)

with log.open("wb") as out:
    process=subprocess.Popen([str(binary),"--silent","--size","1280","720","--saves",str(root)],stdout=out,stderr=subprocess.STDOUT)
    window=None
    try:
        wait_for(find_window,"normal Redux library")
        xdo("windowfocus","--sync",window)
        wait_for(lambda:logged("AngelScript"),"engine initialization")
        time.sleep(2)
        capture("01-redux-library")
        # The synthetic development tile is the first visible library item.
        click(170,176)
        wait_for(lambda:logged("project library opened"),"Blueprint Studio library tile")
        capture("02-project-browser")
        click(140,295)
        click(120,429)
        xdo("type","--delay",50,"First Person Test")
        key("Return")
        path=own/"blueprint-project-First Person Test.bp.json"
        wait_for(lambda:path.is_file() and logged("Created First Person Test from first-person"),"compile and save named template")
        project=json.loads(path.read_text())
        assert len(project["graphs"])==13
        assert all(g.get("comments") for g in project["graphs"])
        assert (own/project["assetRoot"]/"_global/model/robot.glb").stat().st_size>0
        capture("03-initial-graph")
        click(553,53)
        wait_for(lambda:logged("preview started: Screen"),"native first person preview")
        time.sleep(2)
        first=capture("04-first-person")
        key("w",1.0)
        second=capture("05-walk")
        assert first!=second,"Movement did not change the rendered view"
        key("space",0.15)
        capture("06-jump")
        key("Escape")
        wait_for(lambda:logged("preview stopped"),"return from play")
        assert not logged("Preview error:")
        click(675,53)
        wait_for(lambda:bool(list(own.glob("bp-*/Screen/main.as"))),"export native source")
        source=sorted(own.glob("bp-*/Screen/main.as"))[-1]
        assert "bp_player_motion" in source.read_text()
        assert (source.parent.parent/"_global/model/robot.glb").stat().st_size>0
        click(850,18)
        capture("07-reopen-project-library")
        click(500,180)
        wait_for(lambda:logged("Opened "+path.name),"reopen saved project")
        assert json.loads(path.read_text())==project
        print("Native Redux tile, project browser, first-person template, preview, movement, export and reopen passed.",flush=True)
    except Exception:
        if window is not None and process.poll() is None:
            try: capture("failure-state")
            except Exception: pass
        raise
    finally:
        process.terminate()
        process.wait(timeout=10)
        conn.close()
        print(log.read_text(errors="replace"))
