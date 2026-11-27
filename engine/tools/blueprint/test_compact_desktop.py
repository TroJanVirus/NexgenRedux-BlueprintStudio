"""Exercise real compact editor at 1920x1080 and after resize.
Use the native renderer, not a simulated canvas or a screenshot mockup.
"""
import json
import os
import pathlib
import subprocess
import sys
import time
from Xlib import display, protocol, X

binary = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2]).resolve()
root.mkdir(parents=True, exist_ok=False)
log = root / "compact-desktop.log"
project = root / "own" / "blueprint-project-Compact Layout.bp.json"
conn = display.Display()

def xdo(*args):
    return subprocess.check_output(["xdotool", *map(str, args)], text=True,
                                   stderr=subprocess.DEVNULL, timeout=10).strip()

with log.open("wb") as output:
    process = subprocess.Popen([str(binary), "--blueprint-studio", "--silent",
                                "--size", "1920", "1080", "--saves", str(root)],
                               stdout=output, stderr=subprocess.STDOUT)
    def wait_for(predicate, label, timeout=30):
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            if process.poll() is not None:
                raise RuntimeError(f"Application exited while waiting for {label}: {process.returncode}")
            try:
                if predicate():
                    return
            except (OSError, ValueError, KeyError, IndexError, subprocess.CalledProcessError):
                pass
            time.sleep(0.15)
        raise RuntimeError(f"Timed out waiting for {label}")

    try:
        def window_id():
            return int(xdo("search", "--onlyvisible", "--name", "^Nexgen$").splitlines()[0])
        wait_for(window_id, "visible desktop")
        window = window_id()
        win = conn.create_resource_object("window", window)
        xdo("windowfocus", "--sync", window)
        wait_for(lambda: "workspace ready" in log.read_text(errors="replace"), "workspace")
        wait_for(lambda: (win.get_geometry().width, win.get_geometry().height) == (1920, 1080), "1080p client")
        time.sleep(0.5)
        def click(x,y):
            xdo("mousemove", "--window", window, x, y)
            time.sleep(0.1)
            xdo("mousedown",1)
            time.sleep(0.15)
            xdo("mouseup",1)
            time.sleep(0.3)
        def key(name):
            xdo("key",name)
            time.sleep(0.3)
        def enter(value):
            xdo("type","--delay",70,value)
            key("Return")
        def shot(name):
            geo = win.get_geometry()
            subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-y",
                            "-f","x11grab","-video_size",f"{geo.width}x{geo.height}",
                            "-i",os.environ["DISPLAY"],"-frames:v","1","-threads","1",str(root/name)],
                           timeout=20,check=True)
        # At 1080p the right panel is at x=1652 rather than x=1012 scaled by 1.5.
        click(1780,130)
        enter("literal:int")
        click(1780,167)
        click(911,18)
        enter("Compact Layout")
        wait_for(project.is_file,"save project")
        assert any(n["kind"]=="literal:int" for n in json.loads(project.read_text())["graphs"][0]["nodes"])
        shot("graph-1920.png")
        # Centered legacy content modal has offset (320,180).
        click(1027,18)
        click(774,313)
        click(679,352)
        click(765,780)
        wait_for(lambda:"Actor class preview ready" in log.read_text(errors="replace"),"actor preview")
        # Native full-screen class editor adds a child and edits separate X/Y/Z fields.
        click(110,989)
        click(1666,170)
        enter("2.5")
        click(1764,170)
        enter("1.25")
        click(1862,170)
        enter("-0.5")
        click(540,54)
        def place():
            assets=json.loads(project.read_text())["visualAssets"]
            actor=next(a for a in assets if a["kind"]=="actor")
            return json.loads(actor["source"])["entities"][1]["place"]
        wait_for(lambda: place()==[2.5,1.25,-0.5],"saved XYZ values")
        shot("actor-1920.png")
        xdo("windowsize","--sync",window,1280,720)
        wait_for(lambda:(win.get_geometry().width,win.get_geometry().height)==(1280,720),"720p client")
        time.sleep(0.5)
        click(1026,170)
        enter("4.25")
        click(540,54)
        wait_for(lambda:place()==[4.25,1.25,-0.5],"resized actor edit")
        click(81,54)
        click(930,133)
        # A close request exercises normal release of the isolated world.
        msg=protocol.event.ClientMessage(window=win,client_type=conn.intern_atom("WM_PROTOCOLS"),
            data=(32,[conn.intern_atom("WM_DELETE_WINDOW"),X.CurrentTime,0,0,0]))
        win.send_event(msg,event_mask=0)
        conn.sync()
        assert process.wait(timeout=20)==0
        print("Compact desktop graph, centered modal, class preview and independent XYZ edits passed at 1080p and 720p.")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=15)
        print(log.read_text(errors="replace"),flush=True)
        conn.close()
