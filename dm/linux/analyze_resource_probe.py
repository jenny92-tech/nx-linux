#!/usr/bin/env python3
# INPUT: One private resource-probe run log and its optional texture census CSV.
# OUTPUT: Reproducible JSON of completed windows, runtime expansion and shader work.
# POS: Offline analysis only; timings from diagnostic runs are not playable FPS.
"""Summarize DM_LINUX_RESOURCE_PROBE output without reading game data or a device."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path

MIB = 1024 * 1024


def parse_fields(line: str) -> dict[str, str]:
    return dict(part.split("=", 1) for part in line.split() if "=" in part)


def summarize(text: str, textures: list[dict[str, str]],
              first_frame: int = 0, last_frame: int | None = None) -> dict:
    windows = []
    shaders = []
    for line in text.splitlines():
        if " RESOURCE_PATH_PROFILE " in line:
            row = {key: int(value) for key, value in parse_fields(line).items()}
            if any(value < 0 for value in row.values()) or row["frames"] <= 0:
                raise ValueError("Invalid resource profile window")
            # Frame is the inclusive end marker. Exclude partial scene windows.
            if row["frame"] - row["frames"] < first_frame:
                continue
            if last_frame is not None and row["frame"] > last_frame:
                continue
            if windows and row["frame"] - row["frames"] < windows[-1]["frame"]:
                raise ValueError("Overlapping or repeated resource windows")
            windows.append(row)
        elif " RESOURCE_SHADER_LOAD " in line:
            row = parse_fields(line)
            for key in ("stage", "warm", "read_meta_ns", "adapt_ns", "finalize_ns", "total_ns"):
                row[key] = int(row[key])
            if row["warm"] not in (0, 1) or any(row[k] < 0 for k in
                    ("read_meta_ns", "adapt_ns", "finalize_ns", "total_ns")):
                raise ValueError("Invalid shader timing")
            if row["total_ns"] != row["read_meta_ns"] + row["adapt_ns"] + row["finalize_ns"]:
                raise ValueError("Shader timing stages do not sum to total")
            shaders.append(row)
    if not windows:
        raise ValueError("No complete resource profile windows in the requested range")
    counts = ("frames", "uploads", "first_uploads", "reuploads", "refresh_transfers",
              "upload_pass_breaks", "source_bytes", "staged_bytes", "upload_wall_ns",
              "copies", "expanded_copies", "copy_pass_breaks")
    totals = {key: sum(row[key] for row in windows) for key in counts}
    by_shader = defaultdict(list)
    for row in shaders:
        by_shader[(row["key"], row["stage"])].append(row)
    repeated = []
    for (key, stage), rows in by_shader.items():
        if {row["warm"] for row in rows} == {0, 1}:
            cold = [row for row in rows if not row["warm"]]
            repeated.append(dict(key=key, stage=stage, warm_loads=sum(row["warm"] for row in rows),
                cold_loads=len(cold), cold_adapt_ns=sum(row["adapt_ns"] for row in cold)))
    groups = defaultdict(lambda: dict(images=0, allocation_bytes=0, extra_bytes=0,
                                     cpu_baseline_bytes=0))
    expanded = []
    seen = set()
    for raw in textures:
        row = {key: int(value) for key, value in raw.items()}
        if any(value < 0 for value in row.values()) or row["serial"] in seen:
            raise ValueError("Invalid or duplicate live texture census row")
        seen.add(row["serial"])
        group = groups[(row["nvn_format"], row["vk_format"], row["cpu_owned"])]
        group["images"] += 1
        for key in ("allocation_bytes", "extra_bytes", "cpu_baseline_bytes"):
            group[key] += row[key]
        if row["astc_expanded_copies"]:
            logical = 0
            for level in range(row["levels"]):
                w, h, d = (max(1, row[key] >> level) for key in ("width", "height", "depth"))
                logical += ((w + 3) // 4) * ((h + 3) // 4) * 16 * d * row["layers"]
            expanded.append(dict(row, astc4x4_logical_bytes=logical))
    return dict(diagnostic_only=True, playable_fps_valid=False,
        window_start_exclusive=windows[0]["frame"] - windows[0]["frames"],
        window_end_inclusive=windows[-1]["frame"], windows=len(windows), totals=totals,
        per_frame={key: value / totals["frames"] for key, value in totals.items() if key != "frames"},
        last_expanded_live_bytes=windows[-1]["expanded_live_bytes"],
        shader_scope="entire log, including warmup and loading; not the selected steady window",
        shader_loads=len(shaders), shader_warm_loads=sum(row["warm"] for row in shaders),
        shader_cold_loads=sum(not row["warm"] for row in shaders),
        shader_cold_adapt_ns=sum(row["adapt_ns"] for row in shaders if not row["warm"]),
        repeated_warm_cold_shaders=sorted(repeated, key=lambda row: row["cold_adapt_ns"], reverse=True),
        texture_groups=[dict(nvn_format=key[0], vk_format=key[1], cpu_owned=key[2], **value)
                        for key, value in sorted(groups.items(), key=lambda item: item[1]["allocation_bytes"], reverse=True)],
        expanded_targets=sorted(expanded, key=lambda row: row["allocation_bytes"], reverse=True),
        expanded_target_allocation_bytes=sum(row["allocation_bytes"] for row in expanded),
        expanded_astc4x4_logical_bytes=sum(row["astc4x4_logical_bytes"] for row in expanded),
        expansion_limit="Proven copy provenance, not proof that an image can remain compressed. "
                        "ASTC logical bytes exclude driver alignment and do not measure savings.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--textures", type=Path)
    parser.add_argument("--from-frame", type=int, default=0)
    parser.add_argument("--to-frame", type=int)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.from_frame < 0 or (args.to_frame is not None and args.to_frame <= args.from_frame):
        parser.error("Invalid frame range")
    raw = args.log.read_bytes()
    textures = []
    if args.textures:
        with args.textures.open(newline="") as source:
            textures = list(csv.DictReader(source))
    result = summarize(raw.decode("utf-8", errors="replace"), textures, args.from_frame, args.to_frame)
    result["log_sha256"] = hashlib.sha256(raw).hexdigest()
    if args.textures:
        result["textures_sha256"] = hashlib.sha256(args.textures.read_bytes()).hexdigest()
    with args.output.open("x") as destination:
        json.dump(result, destination, indent=2)
        destination.write("\n")
    print(json.dumps({key: result[key] for key in ("windows", "totals", "expanded_target_allocation_bytes",
                                                  "shader_warm_loads", "shader_cold_loads")}))


if __name__ == "__main__":
    main()
