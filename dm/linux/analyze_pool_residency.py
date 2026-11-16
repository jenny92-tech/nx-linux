#!/usr/bin/env python3
# INPUT: C95 pool-window logs and optional read-only allocator/CPU snapshots.
# OUTPUT: Separate logical use, Vulkan backing and measured CPU page residency.
# POS: Offline diagnostics; never controls a device or changes pool capacities.
"""Report pool headroom without treating unused virtual capacity as saved RAM."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def window_log(text: str) -> dict:
    rows = []
    required = ("frame", "logical", "dense", "window_bytes", "windows",
                "peak_window_bytes", "created", "reclaimed", "promoted", "demoted", "upload_waits", "async")
    for line in text.splitlines():
        if " POOL_WINDOW_PROFILE " not in line:
            continue
        fields = dict(word.split("=", 1) for word in line.split() if "=" in word)
        row = {key: int(fields[key]) for key in required}
        if any(value < 0 for value in row.values()) or row["async"] not in (0, 1):
            raise ValueError("Invalid pool-window counters")
        if row["window_bytes"] > row["peak_window_bytes"] or row["reclaimed"] > row["created"]:
            raise ValueError("Inconsistent pool-window counters")
        if rows and (row["frame"] <= rows[-1]["frame"] or any(row[key] < rows[-1][key]
                for key in ("created", "reclaimed", "promoted", "demoted", "upload_waits"))):
            raise ValueError("Restarted or repeated run; supply each run as a separate log")
        rows.append(row)
    if not rows:
        raise ValueError("No POOL_WINDOW_PROFILE records; requires an enabled C95 window runtime")
    return dict(samples=len(rows), first_frame=rows[0]["frame"], last=rows[-1],
                maximum_sampled_backing_bytes=max(x["dense"] + x["window_bytes"] for x in rows),
                lifetime_peak_window_bytes=rows[-1]["peak_window_bytes"],
                backing_scope="Vulkan pool buffers only; excludes images, other allocations and RSS")


def union_bytes(ranges: list[tuple[int, int]]) -> int:
    total = 0
    end = 0
    for low, high in sorted(ranges):
        if low < 0 or high <= low:
            raise ValueError("Invalid window range")
        total += max(0, high - max(end, low))
        end = max(end, high)
    return total


def allocator_snapshot(data: dict) -> dict:
    if not data.get("read_only") or not data.get("stopped_snapshot"):
        raise ValueError("Allocator census must be a read-only stopped snapshot")
    runtime = data["runtime_resources"]
    owners = {row["serial"]: row for row in runtime["pools"]}
    if len(owners) != len(runtime["pools"]):
        raise ValueError("Duplicate logical pool identity")
    windows = runtime.get("windows", [])
    allocated = sum(row["allocation_size"] for row in windows)
    if allocated != runtime["windows_summary"]["bytes"]:
        raise ValueError("Window ledger does not match allocations")
    ranges = {}
    for row in windows:
        owner = owners[row["owner_serial"]]
        offset = row["guest"] - owner["guest"]
        if offset < 0 or row["size"] <= 0 or offset + row["size"] > owner["size"]:
            raise ValueError("Window outside its logical pool")
        ranges.setdefault(row["owner_serial"], []).append((offset, offset + row["size"]))
    native = []
    seen = set()
    for pool in data["pools"]:
        if not pool.get("accounting_verified"):
            raise ValueError("Unverified native allocator accounting")
        header = pool["nvn_header"]
        base, size = header["base"], header["size"]
        used, free = pool["accounted_used_bytes"], pool["free_bytes"]
        if base in seen or min(size, used, free) < 0 or used + free != size:
            raise ValueError("Invalid or duplicate native capacity accounting")
        seen.add(base)
        highest = 0
        for block in pool["allocations"]:
            low = block["raw"] - base
            end = low + block["reserved_bytes"]
            if low < 0 or end > size or end < low:
                raise ValueError("Native allocation outside pool")
            highest = max(highest, end)
        backing = [row for row in owners.values() if row["guest"] == base]
        native.append(dict(flags=header["flags"], capacity_bytes=size, used_bytes=used,
            free_capacity_bytes=free, highest_live_end_bytes=highest,
            allocation_failures=pool["allocation_failures"],
            dense_backing_bytes=sum(row["buffer"]["allocation_size"] for row in backing)))
    window_ranges = [dict(owner_serial=serial, window_range_bytes=sum(b-a for a, b in spans),
                         unique_range_bytes=union_bytes(spans),
                         overlapping_range_bytes=sum(b-a for a, b in spans)-union_bytes(spans))
                     for serial, spans in sorted(ranges.items())]
    return dict(pid=data["pid"], stopped_seconds=data["elapsed_seconds"], native_pools=native,
        window_allocated_bytes=allocated, window_ranges=window_ranges,
        pool_buffer_backing_bytes=allocated + sum(row["buffer"]["allocation_size"] for row in owners.values()),
        limitation="Free capacity and overlapping ranges are diagnostic headroom, not measured reclaimable RAM or a safe capacity limit.")


def cpu_snapshot(data: dict) -> dict:
    if not data.get("read_only") or not data.get("stable_double_walk"):
        raise ValueError("CPU census needs a stable read-only double walk")
    keys = ("capacity_bytes", "nonfree_including_headers_bytes", "free_bytes", "largest_free_bytes",
            "resident_bytes", "resident_free_interior_bytes")
    result = {key: data[key] for key in keys}
    if (any(type(value) is not int or value < 0 for value in result.values()) or
            result["resident_free_interior_bytes"] > result["resident_bytes"] or
            result["resident_bytes"] > result["capacity_bytes"] or
            result["largest_free_bytes"] > result["free_bytes"] or
            result["free_bytes"] + result["nonfree_including_headers_bytes"] > result["capacity_bytes"]):
        raise ValueError("Invalid CPU residency accounting")
    return dict(result, atomic_snapshot=bool(data.get("atomic_snapshot")),
        limitation="Double walks do not hold allocator locks; free resident pages must not be discarded from this address census.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", type=Path, nargs="*")
    parser.add_argument("--snapshot", type=Path, action="append", default=[])
    parser.add_argument("--cpu", type=Path, action="append", default=[])
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if not (args.logs or args.snapshot or args.cpu):
        parser.error("provide a log, --snapshot or --cpu")
    report = dict(logs=[], snapshots=[], cpu=[], units="bytes; divide by 1048576 for MiB", measured_savings=False)
    for kind, paths, analyze in (("logs", args.logs, window_log),
                                  ("snapshots", args.snapshot, allocator_snapshot),
                                  ("cpu", args.cpu, cpu_snapshot)):
        for path in paths:
            raw = path.read_bytes()
            value = raw.decode() if kind == "logs" else json.loads(raw)
            report[kind].append(dict(path=str(path), sha256=hashlib.sha256(raw).hexdigest(), **analyze(value)))
    output = json.dumps(report, indent=2) + "\n"
    if args.out:
        with args.out.open("x") as handle:
            handle.write(output)
    else:
        print(output, end="")


if __name__ == "__main__":
    main()
