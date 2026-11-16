#!/usr/bin/env python3
# INPUT:  Existing bogo-builder:dev image and locally prepared Linux guest artifacts.
# OUTPUT:  Bounded original-code execution, durable logs and per-run receipts.
# POS:    Run ARM64 Linux bring-up in the existing Docker VM without touching saves.
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("load", "os-init", "os-services", "sdk-startup", "game"))
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 600:
        parser.error("timeout must be 1..600 seconds")
    os.umask(0o077)
    root = Path(__file__).resolve().parents[2]
    output = root / "dm/linux/out"
    token = str(time.time_ns())
    run = output / "runs" / (args.mode + "-" + token)
    for name in ("save", "temp"):
        (run / name).mkdir(parents=True, exist_ok=False)
    container_run = "/out/" + run.relative_to(output).as_posix()
    container = "d2r-linux-host-" + token
    command = ["docker", "run", "--rm", "--pull=never", "--name", container,
        "--platform", "linux/amd64", "--cpus", "2", "--memory", "4g", "--read-only",
        "--tmpfs", "/tmp:rw,nosuid,size=256m", "--mount", f"type=bind,src={output},dst=/out"]
    settings = {"DM_SVC": "1", "DM_DBG": "1", "DM_OPMODE": "0", "DM_APP_POOL_ADD_MB": "0",
        "DM_ROMFS": "/out/game-1.0.28.0/Update/RomFS",
        "DM_SAVE": container_run + "/save", "DM_TEMP": container_run + "/temp"}
    for key, value in settings.items():
        command += ["--env", key + "=" + value]
    command += ["bogo-builder:dev", "/usr/aarch64-linux-gnu/lib/ld-linux-aarch64.so.1",
        "--library-path", "/usr/aarch64-linux-gnu/lib", "/out/d2r-linux-host-probe",
        "/out/game-1.0.28.0/linux-native", container_run + "/result.json", args.mode]
    started = time.monotonic()
    with (run / "stdout.log").open("w") as log:
        try:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
            code = result.returncode
        except subprocess.TimeoutExpired:
            subprocess.run(["docker", "kill", container], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            code = 124
    text = (run / "stdout.log").read_text()
    receipt = {"mode": args.mode, "exit_code": code, "seconds": time.monotonic() - started,
        "environment": "local Docker ARM64 Linux VM, not the RK3566 device",
        "nnmain_call_reached": "LINUX_ORIGINAL nnMain" in text, "game_frames_verified": 0,
        "log": str(run / "stdout.log"), "result": str(run / "result.json")}
    (run / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(text[-7000:])
    print(json.dumps(receipt, indent=2))
    return code if code >= 0 else 128 - code


if __name__ == "__main__":
    sys.exit(main())
