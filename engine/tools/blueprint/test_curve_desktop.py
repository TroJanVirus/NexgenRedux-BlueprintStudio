"""Verify the native curve editor via actual X11 mouse input."""
import json
import pathlib
import subprocess
import sys
import time
from Xlib import X, display

binary = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2]).resolve()
own = root / "own"
own.mkdir(parents=True)
recovery = own / "blueprint-recovery.bp.json"
conn = display.Display()
log = root / "curve.log"

def xdo(*args):
    return subprocess.check_output(["xdotool", *map(str,args)], text=True,
                                   stderr=subprocess.DEVNULL, timeout=10).strip()

def wait_for(predicate, label):
    until = time.monotonic()+30
    while time.monotonic()<until:
        if proc.poll() is not None:
            raise RuntimeError(f"Editor exited during {label}: {proc.returncode}")
        try:
            if predicate():
                return
        except (OSError, ValueError, KeyError, IndexError, subprocess.CalledProcessError):
            pass
        time.sleep(.15)
    raise RuntimeError("Timed out: "+label)

def move(x,y):
    xdo("mousemove","--window",window,x,y)
    time.sleep(.15)

def click(x,y,button=1):
    move(x,y)
    xdo("mousedown",button)
    time.sleep(.2)
    xdo("mouseup",button)
    time.sleep(.35)

def drag(x,y,a,b):
    move(x,y)
    xdo("mousedown",1)
    time.sleep(.2)
    move(a,b)
    time.sleep(.25)
    xdo("mouseup",1)
    time.sleep(.35)

def key(name):
    xdo("keydown",name)
    time.sleep(.2)
    xdo("keyup",name)
    time.sleep(.35)

def saved_node():
    doc=json.loads(recovery.read_text())
    return next(n for n in doc["graphs"][0]["nodes"] if n["kind"]=="curve:cubic")

def capture(name):
    # Native renderer evidence, not an illustration.
    import os
    subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-y",
                    "-f","x11grab","-video_size","1280x720","-i",os.environ["DISPLAY"],
                    "-frames:v","1","-threads","1",str(root/name)],check=True,timeout=20)

with log.open("wb") as output:
    proc=subprocess.Popen([str(binary),"--blueprint-studio","--silent","--size","1280","720",
                           "--saves",str(root)],stdout=output,stderr=subprocess.STDOUT)
    try:
        wait_for(lambda: bool(xdo("search","--onlyvisible","--name","^Nexgen$")),"window")
        window=int(xdo("search","--onlyvisible","--name","^Nexgen$").splitlines()[0])
        xdo("windowfocus","--sync",window)
        wait_for(lambda:"workspace ready" in log.read_text(errors="replace"),"workspace")
        time.sleep(.5)
        click(500,100,3)
        xdo("type","--delay",35,"curve:cubic")
        key("Return")
        wait_for(saved_node,"new curve node")
        original=saved_node().get("values",{})
        click(580,112)
        click(1145,550)
        wait_for(lambda:"cubic curve editor opened" in log.read_text(errors="replace"),"curve editor")
        capture("curve-before.png")
        drag(484,207,520,275)
        capture("curve-draft.png")
        click(995,575)
        assert saved_node().get("values",{})==original
        click(1145,550)
        drag(484,207,520,275)
        click(870,575)
        wait_for(lambda:saved_node().get("values",{})!=original,"applied curve checkpoint")
        points=saved_node()["values"]
        assert len(points)==4,points
        x,y=map(float,points["p1"].split(","))
        assert x>420 and y>100,(x,y)
        key("ctrl+z")
        wait_for(lambda:saved_node().get("values",{})==original,"single undo")
        key("ctrl+y")
        wait_for(lambda:saved_node().get("values",{})==points,"single redo")
        print("Native cubic curve: drag, cancel, apply, undo, redo verified.",flush=True)
    finally:
        proc.terminate()
        try: proc.wait(timeout=10)
        except subprocess.TimeoutExpired: proc.kill();proc.wait()
