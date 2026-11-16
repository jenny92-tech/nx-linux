#!/usr/bin/env python3
# INPUT: A running diagnostic D2R session, its PID/log and local scale control.
# OUTPUT: Warm 60-frame windows per scale and a JSON measurement receipt.
# POS: Host-side same-scene experiment; never injects global input or starts games.
"""Keep the character stationary in camp; stop readbacks during measurement.

The session must opt into DM_LINUX_3D_SCALE_CONTROL and DM_PROFILE_DETAIL.
Only the target D2R process consumes the PID-scoped /run control file.
"""
import argparse
import json
import re
import shlex
import subprocess
import time
from pathlib import Path

WINDOW = re.compile(r"RENDER_PROFILE frame=(\d+) frames=60 fps=([\d.]+) elapsed_ms=([\d.]+) pipeline_ms=([\d.]+) gpu_wait_ms=([\d.]+) upload_decode_ms=([\d.]+) submits=(\d+) draws=(\d+)")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--adb", default="adb")
    p.add_argument("--serial", required=True)
    p.add_argument("--pid", required=True, type=int)
    p.add_argument("--run", required=True, help="Absolute device run directory")
    p.add_argument("--control", required=True, help="Absolute /run scale-control file")
    p.add_argument("--scales", type=float, nargs="+", default=[1., .75, .5, 1.])
    p.add_argument("--windows", type=int, default=2)
    p.add_argument("--timeout", type=int, default=300, help="Seconds per phase")
    p.add_argument("--sequence", type=int, default=1, help="First unused sequence")
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    if (a.pid <= 0 or a.windows < 1 or a.timeout < 10 or a.sequence < 1 or
            not a.run.startswith("/") or not a.control.startswith("/run/") or
            any(not .5 <= s <= 1. for s in a.scales)):
        p.error("Invalid PID, limits, absolute paths or scales (0.5..1.0)")

    def shell(command):
        return subprocess.check_output([a.adb, "-s", a.serial, "shell", command], text=True, timeout=20)

    comm = shell(f"cat /proc/{a.pid}/comm").strip()
    if comm not in {"d2r-linux", "d2r-linux-novid"}:
        p.error("Target PID is not D2R")
    a.output.parent.mkdir(parents=True, exist_ok=True)
    phases = []
    for seq, scale in enumerate(a.scales, a.sequence):
        offset = int(shell("wc -c < " + shlex.quote(a.run + "/stdout.log"))) + 1
        payload = f"{a.pid} {seq} {scale}\n"
        temporary = shlex.quote(a.control + ".next")
        shell(f"umask 077; printf %s {shlex.quote(payload)} > {temporary} && mv {temporary} {shlex.quote(a.control)}")
        deadline = time.monotonic() + a.timeout
        marker = f"LINUX_3D_SCALE_REQUEST sequence={seq} "
        while time.monotonic() < deadline:
            log = shell(f"tail -c +{offset} " + shlex.quote(a.run + "/stdout.log"))
            if "LINUX_GUEST_SIGNAL" in log or "rebuild postcondition failed" in log:
                raise RuntimeError("D2R failed; inspect the session log")
            if marker in log:
                section = log[log.rindex(marker):]
                # For changed scale, start after the completed original rebuild.
                end = re.search(rf"LINUX_3D_SCALE_END sequence={seq} .*", section)
                if end:
                    section = section[end.end():]
                windows = []
                for m in WINDOW.finditer(section):
                    values = m.groups()
                    windows.append(dict(frame=int(values[0]), fps=float(values[1]),
                                        elapsed_ms=float(values[2]), pipeline_ms=float(values[3]),
                                        gpu_wait_ms=float(values[4]), upload_decode_ms=float(values[5]),
                                        submits=int(values[6]), draws=int(values[7])))
                # The first window can overlap the request/rebuild: discard it.
                if len(windows) >= a.windows + 1:
                    measured = windows[1:a.windows + 1]
                    phase = dict(sequence=seq, scale=scale, windows=measured,
                                 fps=60000 * len(measured) / sum(w["elapsed_ms"] for w in measured),
                                 rebuild=end.group(0) if end else None)
                    phases.append(phase)
                    a.output.write_text(json.dumps(dict(pid=a.pid, run=a.run, phases=phases), indent=2) + "\n")
                    print(json.dumps(phase), flush=True)
                    break
            time.sleep(3)
        else:
            raise TimeoutError(f"Scale {scale}: no complete warm measurement before timeout")


if __name__ == "__main__":
    main()
