#!/usr/bin/env python3
# INPUT: Original 90447 RomFS, prepared ASTC work, validated full-code shader cache.
# OUTPUT: Verified smaller resource pack and pre-inflated shaders in a Zstd image.
# POS: Computer-only preparation; no game execution, device access or save changes.
"""Prepare RK3566 resources on the computer; see README.md for the exact command."""
import argparse
import gzip
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
from collections import Counter
from pathlib import Path

from handheld_mips import NativeLayout, FORMATS, logical_path, trim_texture, world_texture

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "dm/tools"))
import casc_overlay as casc


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(8 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def prepare_textures(work, output, maximum, native):
    cache_path = work / "files/data/hd/texture_desc_cache.json"
    entries = json.loads(cache_path.read_text(encoding="utf-8"))
    replacements = {}
    for line in (work / "repl.tsv").read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        logical = logical_path(line.split("\t", 1)[0])
        if logical in replacements:
            raise ValueError("duplicate ASTC replacement: " + logical)
        source = work / "files" / logical
        if not source.is_file():
            raise ValueError("missing ASTC prepared file: " + str(source))
        replacements[logical] = source
    if replacements.get("data/hd/texture_desc_cache.json") != cache_path:
        raise ValueError("ASTC work lacks its matching descriptor cache")
    changed, skipped = [], Counter()
    before = after = 0
    for i, entry in enumerate(entries):
        name = logical_path(entry["filename"])
        if not world_texture(name) or entry["format"] not in FORMATS:
            skipped["outside supported world ASTC textures"] += 1
            continue
        if entry["type"] != 2 or entry["depth"] != 1 or entry["layoutType"] != 3:
            skipped["non-2D packaged texture"] += 1
            continue
        if max(entry["width"], entry["height"]) <= maximum:
            skipped["already within limit"] += 1
            continue
        if name not in replacements:
            raise ValueError("ASTC descriptor lacks converted payload: " + name)
        original = replacements[name].read_bytes()
        try:
            result, updated, levels = trim_texture(original, entry, maximum, native)
        except ValueError as error:
            raise ValueError(name + ": " + str(error)) from error
        if not levels:
            skipped["insufficient existing mips/thumbnail"] += 1
            continue
        destination = output / "files" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(result)
        replacements[name] = destination
        entries[i] = updated
        before += len(original)
        after += len(result)
        changed.append({"path": name, "old_width": entry["width"], "old_height": entry["height"],
                        "width": updated["width"], "height": updated["height"], "mips_removed": levels,
                        "source_sha256": hashlib.sha256(original).hexdigest(),
                        "sha256": hashlib.sha256(result).hexdigest()})
        if len(changed) % 500 == 0:
            print(f"textures: {len(changed)} reduced; {before - after:,} payload bytes removed", flush=True)
    destination = output / "files/data/hd/texture_desc_cache.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(entries, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    replacements["data/hd/texture_desc_cache.json"] = destination
    with (output / "replacements.tsv").open("w", encoding="utf-8") as stream:
        for name, path in sorted(replacements.items()):
            stream.write(name + "\t" + str(path.resolve()) + "\n")
    report = {"maximum_world_edge": maximum, "changed": len(changed), "skipped": dict(skipped),
              "changed_source_bytes": before, "changed_output_bytes": after,
              "retained_mips_native_deswizzle_verified": True, "fonts_ui_sprites_resized": False,
              "records": changed}
    write_json(output / "texture-report.json", report)
    return replacements, report


def shader_index_keys(data):
    if len(data) < 12:
        raise ValueError("truncated shader index")
    magic, version, count = struct.unpack_from("<3I", data)
    if magic != 0x494b5644 or version != 2 or not 0 < count <= 1000000:
        raise ValueError("a validated full-code v2 shader index is required")
    result, cursor = {}, 12
    for _ in range(count):
        if cursor + 24 + 0x880 > len(data):
            raise ValueError("truncated shader index entry")
        key = data[cursor:cursor + 16].decode("ascii")
        stage, size = struct.unpack_from("<II", data, cursor + 16)
        control = data[cursor + 24:cursor + 24 + 0x880]
        if (len(key) != 16 or any(c not in "0123456789abcdef" for c in key) or key in result or
            stage not in (0, 1, 5) or not 0 < size <= 16 * 1024 * 1024 or
            struct.unpack_from("<3I", control) != (0x98761234, 1, 16) or
            struct.unpack_from("<I", control, 0x704)[0] != size or
            struct.unpack_from("<I", control, 0x714)[0] != stage):
            raise ValueError("invalid shader index entry")
        cursor += 24 + 0x880 + size
        if cursor > len(data):
            raise ValueError("shader code outside index")
        result[key] = stage
    if cursor != len(data):
        raise ValueError("trailing shader index data")
    return result


def prepare_shaders(source, output):
    receipt = source / "merge-receipt.json"
    if not receipt.is_file():
        receipt = source / "validation.json"
    proof = json.loads(receipt.read_text(encoding="utf-8"))
    index = (source / "shader_index.bin").read_bytes()
    if hashlib.sha256(index).hexdigest() != proof["index_sha256"]:
        raise ValueError("shader index differs from validation receipt")
    keys = shader_index_keys(index)
    records = {r["key"]: r for r in proof["records"] if r.get("validated")}
    if not keys.keys() <= records.keys():
        raise ValueError("unvalidated shader index entry")
    output.mkdir()
    (output / "shader_index.bin").write_bytes(index)
    prepared, compressed_bytes, raw_bytes = [], 0, 0
    for number, (key, stage) in enumerate(keys.items(), 1):
        record = records[key]
        metadata = (source / (key + ".vkmeta")).read_bytes()
        compressed = (source / (key + ".spv.gz")).read_bytes()
        if (hashlib.sha256(metadata).hexdigest() != record["metadata_sha256"] or
            hashlib.sha256(compressed).hexdigest() != record["gzip_sha256"]):
            raise ValueError("shader payload differs from validation receipt: " + key)
        if len(metadata) < 48:
            raise ValueError("shader metadata header: " + key)
        header = struct.unpack_from("<12I", metadata)
        if (header[:3] != (0x4d4b5644, 1, stage) or
            header[3] > 64 or header[4] > 32 or header[6] > 32 or header[5] > 4096 or
            len(metadata) != 48 + header[3] * 32 + header[4] * 8 + header[6] * 4 + header[5] or
            not 20 <= header[11] <= 16 * 1024 * 1024 or header[11] % 4):
            raise ValueError("shader metadata bounds: " + key)
        raw = gzip.decompress(compressed)
        if (len(raw) != header[11] or raw[:4] != bytes.fromhex("03022307") or
            hashlib.sha256(raw).hexdigest() != record["spirv_sha256"]):
            raise ValueError("shader decompressed payload: " + key)
        # This field only selects .spv versus .spv.gz; shader instructions,
        # conditional runtime guards and all binding metadata stay identical.
        changed = metadata[:44] + bytes(4) + metadata[48:]
        (output / (key + ".vkmeta")).write_bytes(changed)
        (output / (key + ".spv")).write_bytes(raw)
        compressed_bytes += len(compressed)
        raw_bytes += len(raw)
        prepared.append({"key": key, "stage": stage,
                         "spirv_sha256": record["spirv_sha256"],
                         "source_metadata_sha256": record["metadata_sha256"],
                         "metadata_sha256": hashlib.sha256(changed).hexdigest(),
                         "runtime_buffer_slots": record.get("runtime_buffer_slots", [])})
        if number % 1000 == 0:
            print(f"shaders: {number}/{len(keys)} verified and pre-inflated", flush=True)
    return {"programs": len(keys), "index_sha256": proof["index_sha256"],
            "inventoried_supported_programs": proof.get("inventoried_supported_programs"),
            "excluded": proof.get("excluded", []), "excluded_stages": proof.get("excluded_stages", []),
            "full_game_shader_coverage": proof.get("full_game_shader_coverage", False),
            "source_receipt_sha256": sha256(receipt), "input_gzip_bytes": compressed_bytes,
            "raw_spirv_bytes": raw_bytes, "shader_code_changed": False,
            "metadata_change": "only gzip length/selection field cleared", "records": prepared}


def verify_pack(pack, replacements, manifest):
    rows = casc.read_prebuilt(pack / "pack_index.tsv")
    if rows.keys() != replacements.keys():
        raise ValueError("resource pack entry set mismatch")
    streams = {}
    try:
        for name, (ekey, encoded_size, size) in rows.items():
            filename = pack / casc.archive_filename(ekey[9],ekey[10])
            if filename not in streams:
                streams[filename] = filename.open("rb")
            stream = streams[filename]
            stream.seek(int.from_bytes(ekey[11:16], "big"))
            encoded = stream.read(encoded_size)
            if len(encoded) != encoded_size or casc.ekey_for(encoded, ekey[9], ekey[10], int.from_bytes(ekey[11:16], "big")) != ekey:
                raise ValueError("resource pack EKey/length mismatch: " + name)
            espec = manifest[name][8]
            if espec == "z":
                decoded = __import__("zlib").decompress(encoded)
            elif espec == "n":
                decoded = encoded
            else:
                decoded = casc.read_data_file(pack, ekey, encoded_size)
            if len(decoded) != size or decoded != replacements[name].read_bytes():
                raise ValueError("resource pack round-trip mismatch: " + name)
    finally:
        for stream in streams.values():
            stream.close()
    return len(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cache = Path(__file__).resolve().parent / "out"
    parser.add_argument("--game-build", choices=("90447", "94012"), default="90447")
    parser.add_argument("--romfs", type=Path, default=cache / "game-1.0.28.0/Update/RomFS")
    parser.add_argument("--astc-work", type=Path, default=cache / "astc-90447/work", help="existing astc_pack.py work directory")
    parser.add_argument("--shader-cache", type=Path, default=cache / "game-1.0.28.0/shaders-runtime-r4", help="validated shader_cache.py/merged runtime directory")
    parser.add_argument("--out", type=Path, required=True, help="new output directory; never overwritten")
    parser.add_argument("--max-side", type=int, default=128, help="world textures only; retain existing mips (default 128)")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--mksquashfs", default=shutil.which("mksquashfs"), help="existing host mksquashfs with Zstd")
    parser.add_argument("--shader-compression-level", type=int, default=19, help="computer-side Zstd level 1..22 (default 19)")
    args = parser.parse_args()
    if not 16 <= args.max_side <= 4096 or not 1 <= args.jobs <= 16 or not 1 <= args.shader_compression_level <= 22:
        parser.error("max-side must be 16..4096; jobs 1..16; shader-compression-level 1..22")
    if not args.mksquashfs:
        parser.error("provide an existing --mksquashfs executable")
    args.romfs, args.astc_work, args.shader_cache, args.out = (
        p.resolve() for p in (args.romfs, args.astc_work, args.shader_cache, args.out))
    source_manifest = args.romfs / "data/build_manifest.psv"
    with source_manifest.open(encoding="utf-8") as stream:
        prefix = "".join(next(stream) for _ in range(12))
    if "## build-name = "+args.game_build+"\n" not in prefix:
        raise ValueError("only the reviewed original 90447 build is supported")
    astc_proof = args.astc_work.parent / "verified.json"
    if not astc_proof.is_file():
        raise ValueError("ASTC work needs its verified.json provenance receipt")
    astc = json.loads(astc_proof.read_text(encoding="utf-8"))
    if not astc.get("passed") or astc.get("game_build") != args.game_build or astc["source_manifest_sha256"] != sha256(source_manifest):
        raise ValueError("ASTC preparation belongs to another source RomFS")
    for name, expected in astc.get("files", {}).items():
        source = args.astc_work / "files" / logical_path(name)
        if sha256(source) != expected:
            raise ValueError("ASTC work changed after verification: " + name)
    args.out.mkdir(parents=True, exist_ok=False)
    work, device = args.out / ".work", args.out / "device"
    work.mkdir()
    device.mkdir()
    native = NativeLayout(work / "native", args.cc)
    replacements, textures = prepare_textures(args.astc_work, work, args.max_side, native)
    shader_report = prepare_shaders(args.shader_cache, work / "shaders")
    write_json(args.out / "shader-report.json", shader_report)
    print("Packing preprocessed world textures on computer...", flush=True)
    pack = work / "pack"
    casc.write_pack(args.romfs, pack, replacements, (0, 9), jobs=args.jobs)
    count = verify_pack(pack, replacements, casc.read_manifest(args.romfs))
    overlay = work / "overlay"
    casc.make_overlay(args.romfs, overlay, {}, casc.read_prebuilt(pack / "pack_index.tsv"))
    for path in pack.glob("*.data"):
        os.link(path, overlay / "data" / path.name)
    base_files, files = [], []
    for path in sorted(overlay.rglob("*")):
        if path.is_dir():
            continue
        relative = path.relative_to(overlay).as_posix()
        if path.is_symlink():
            base_files.append({"path": relative, "size": path.stat().st_size})
        else:
            destination = device / "RomFS" / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            os.link(path, destination)
    image = device / "shaders.squashfs"
    subprocess.run([args.mksquashfs, str(work / "shaders"), str(image), "-noappend", "-comp", "zstd",
                    "-Xcompression-level", str(args.shader_compression_level), "-b", "262144", "-processors", str(args.jobs),
                    "-no-xattrs", "-all-root", "-no-progress", "-mkfs-time", "0"], check=True)
    installer = Path(__file__).with_name("install_handheld_pack.py")
    shutil.copyfile(installer, device / installer.name)
    for path in sorted(device.rglob("*")):
        if path.is_file():
            files.append({"path": path.relative_to(device).as_posix(), "size": path.stat().st_size,
                          "sha256": sha256(path)})
    summary = {"schema": 1, "game_build": args.game_build, "maximum_world_edge": args.max_side,
               "preparation_location": "computer", "source_manifest_sha256": sha256(source_manifest),
               "base_config_sha256": sha256(args.romfs / "data/.build.config"),
               "astc_proof_sha256": sha256(astc_proof), "textures_changed": textures["changed"],
               "texture_payload_bytes_removed": textures["changed_source_bytes"] - textures["changed_output_bytes"],
               "resource_entries_roundtrip_verified": count, "shader_programs": shader_report["programs"],
               "shader_programs_not_exported": shader_report["excluded"],
               "unsupported_shader_stages": shader_report["excluded_stages"],
               "full_game_shader_coverage": shader_report["full_game_shader_coverage"],
               "shader_index_sha256": shader_report["index_sha256"], "shader_instructions_unchanged": True,
               "shader_gzip_inflate_on_device": False, "shader_filesystem_compressor": "zstd level " + str(args.shader_compression_level),
               "fonts_ui_sprites_resized": False, "realtime_gpu_rendering_precomputed": False,
               "gameplay_verified": False, "files": files, "base_files": base_files,
               "tools": {name: sha256(ROOT / name) for name in [
                   "dm/linux/prepare_handheld.py", "dm/linux/handheld_mips.py",
                   "dm/linux/install_handheld_pack.py", "dm/tools/casc_overlay.py",
                   "dm/gfx/nvn/nvn_tex.c", "dm/gfx/nvn/nvn_tex.h",
                   "dm/gfx/tex/nvn_deswizzle.c", "dm/gfx/tex/nvn_deswizzle.h"]}}
    write_json(device / "manifest.json", summary)
    write_json(args.out / "summary.json", {k: v for k, v in summary.items() if k not in ("files", "base_files")})
    print(json.dumps({k: v for k, v in summary.items() if k not in ("files", "base_files")}, indent=2), flush=True)
    print("Copy only the device/ directory; .work/ stays on the computer.", flush=True)


if __name__ == "__main__":
    main()
