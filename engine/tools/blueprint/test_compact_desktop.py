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
            xdo("keydown",name)
            time.sleep(0.18)
            xdo("keyup",name)
            time.sleep(0.3)
        def drag(x1,y1,x2,y2):
            xdo("mousemove","--window",window,x1,y1)
            time.sleep(0.15)
            xdo("mousedown",1)
            time.sleep(0.18)
            xdo("mousemove","--window",window,x2,y2)
            time.sleep(0.3)
            xdo("mouseup",1)
            time.sleep(0.4)
        def enter(value):
            xdo("type","--delay",70,"--",value)
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
        click(1666,218)
        enter("2.5")
        click(1764,218)
        enter("1.25")
        click(1862,218)
        enter("-0.5")
        click(540,54)
        def place():
            assets=json.loads(project.read_text())["visualAssets"]
            actor=next(a for a in assets if a["kind"]=="actor")
            return json.loads(actor["source"])["entities"][1]["place"]
        wait_for(lambda: place()==[2.5,1.25,-0.5],"saved XYZ values")
        shot("actor-1920.png")
        # Collapse and expand the Transform category without altering the asset.
        click(1760,181)
        shot("actor-collapsed-details.png")
        click(1760,181)
        def actor_root():
            assets=json.loads(project.read_text())["visualAssets"]
            actor=next(a for a in assets if a["kind"]=="actor")
            return json.loads(actor["source"])["entities"][0]
        # Root is at the centre of the viewport; the projected positive Y handle is 52 px up.
        click(100,134)
        time.sleep(0.4)
        original=actor_root().get("place",[0,0,0])
        drag(929,525,929,475)
        click(540,54)
        wait_for(lambda:actor_root().get("place",[0,0,0])!=original,"native transform drag")
        moved=actor_root()["place"]
        assert moved[0]==original[0] and moved[1]>original[1] and moved[2]==original[2]
        key("ctrl+z")
        click(540,54)
        wait_for(lambda:actor_root().get("place",[0,0,0])==original,"one-step gizmo undo")
        # Divider is a real hit target; resize the right panel and then restore it.
        drag(1610,700,1540,700)
        shot("actor-wide-inspector.png")
        drag(1540,700,1610,700)
        # Return to the child and resize the client without losing selection.
        click(100,164)
        xdo("windowsize","--sync",window,1280,720)
        wait_for(lambda:(win.get_geometry().width,win.get_geometry().height)==(1280,720),"720p client")
        time.sleep(0.5)
        click(1026,218)
        enter("4.25")
        click(540,54)
        wait_for(lambda:place()==[4.25,1.25,-0.5],"resized actor edit")
        # Jump from the class viewport to Construction and back, without closing the project.
        click(978,54)
        click(128,54)
        time.sleep(0.4)
        shot("actor-returned-tab.png")
        click(81,54)
        click(930,133)
        # Open two graph tabs and return to the original by its tab.
        click(100,90)
        click(100,122)
        shot("graph-tabs.png")
        click(285,660)
        click(1200,688)
        shot("graph-content-drawer.png")
        wait_for(lambda:"native content preview ready" in log.read_text(errors="replace"),"native thumbnail")
        click(850,514)
        shot("content-floating.png")
        # Floating default bounds: x=171, y=351, right=791. Dock button is near x=629.
        click(629,365)
        shot("content-redocked.png")
        click(310,560)
        shot("actor-from-drawer.png")
        drag(970,470,940,470)
        shot("actor-tabs.png")
        # A close request exercises normal release of the isolated world.
        msg=protocol.event.ClientMessage(window=win,client_type=conn.intern_atom("WM_PROTOCOLS"),
            data=(32,[conn.intern_atom("WM_DELETE_WINDOW"),X.CurrentTime,0,0,0]))
        win.send_event(msg,event_mask=0)
        conn.sync()
        assert process.wait(timeout=20)==0
        layout=json.loads((root/"own"/"blueprint-editor-layout.json").read_text())
        assert layout["version"]==1 and layout["actorRight"]==340 and layout["content"] is True, layout
        # Restart the application, rather than only checking the written file.
        with (root/"layout-restart.log").open("wb") as restarted_log:
            restarted=subprocess.Popen([str(binary),"--blueprint-studio","--silent","--size","1280","720","--saves",str(root)],stdout=restarted_log,stderr=subprocess.STDOUT)
            try:
                until=time.monotonic()+30
                while time.monotonic()<until and restarted.poll() is None:
                    if "restored editor layout" in (root/"layout-restart.log").read_text(errors="replace"):break
                    time.sleep(0.15)
                else:raise RuntimeError("Editor did not restore layout on restart")
            finally:
                if restarted.poll() is None:restarted.terminate()
                restarted.wait(timeout=15)
        print("Compact desktop graph, centered modal, class preview and independent XYZ edits passed at 1080p and 720p.")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=15)
        print(log.read_text(errors="replace"),flush=True)
        conn.close()
