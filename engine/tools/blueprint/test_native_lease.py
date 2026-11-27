"""Cross-platform process-level test of the real Redux authoring lease."""
import json
from pathlib import Path
import subprocess
import sys
import time

binary = Path(sys.argv[1]).resolve(strict=True)
root = Path(sys.argv[2]).resolve()
root.mkdir(parents=True, exist_ok=False)
shared = root / "shared"
independent = root / "independent"
(shared / "own").mkdir(parents=True)
independent.mkdir()
recovery = shared / "own" / "blueprint-recovery.bp.json"
draft = dict(version=2, kind="NativeBlueprint", name="Lease Test", assetRoot="", graphs=[
    dict(owner="Screen", name=name, kind="event",
         nodes=[dict(id="entry", kind="entry", x=24, y=60)], links=[])
    for name in ("onEnter", "update", "draw", "onSuspend", "onResume", "onExit")
])
original = json.dumps(draft).encode("utf-8")
recovery.write_bytes(original)


def args(directory, frames=None):
    result = [str(binary), "--blueprint-studio", "--null", "--saves", str(directory)]
    if frames is not None:
        result += ["--frames", str(frames)]
    return result


def run(label, directory, expect_success):
    log = root / (label + ".log")
    with log.open("wb") as out:
        result = subprocess.run(args(directory, 60), stdout=out,
                                stderr=subprocess.STDOUT, timeout=60)
    message = log.read_text(errors="replace")
    print(label, message, flush=True)
    if expect_success:
        assert result.returncode == 0 and "workspace ready" in message
        assert "EXIT OK" in message
    else:
        assert result.returncode != 0
        assert "save directory is busy or unavailable" in message
        assert "workspace ready" not in message


holder_log = root / "holder.log"
with holder_log.open("wb") as out:
    # No fixed frame limit: hold the lease until forcibly terminated.
    holder = subprocess.Popen(args(shared), stdout=out, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 45
        while "workspace ready" not in holder_log.read_text(errors="replace"):
            if holder.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("First native process did not acquire the lease")
            time.sleep(0.1)
        assert "Recovered unsaved Blueprint" in holder_log.read_text(errors="replace")
        run("contender", shared, False)
        assert recovery.read_bytes() == original
        run("independent", independent, True)
        assert holder.poll() is None
    finally:
        if holder.poll() is None:
            holder.kill()
        holder.wait(timeout=15)
        print(holder_log.read_text(errors="replace"), flush=True)

# An abandoned lock file is not a stale lock. The OS releases ownership on death.
run("after-crash", shared, True)
assert recovery.read_bytes() == original
# A normal shutdown must also release it, and preserve the same recovery draft.
run("after-normal-exit", shared, True)
assert recovery.read_bytes() == original
print("Native process contention, independent directories, forced and normal release passed.",
      flush=True)
