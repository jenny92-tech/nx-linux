#!/usr/bin/env python3
# INPUT: A prepared device/ directory and the already installed original RomFS.
# OUTPUT: Verified payloads and a receipt for the existing base-resource fallback.
# POS: One-time verification; works on exFAT without links or a second game copy.
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tempfile


def safe_path(root, name):
    relative = PurePosixPath(name)
    if relative.is_absolute() or ".." in relative.parts or not relative.parts:
        raise ValueError("unsafe pack path")
    path = root / str(relative)
    if not path.parent.resolve().is_relative_to(root.resolve()):
        raise ValueError("pack path escapes through a symlink")
    return path


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(4 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _write_receipt(pack, receipt):
    destination = pack / "installed.json"
    if destination.is_symlink():
        raise ValueError("installed receipt must not be a symbolic link")
    descriptor, name = tempfile.mkstemp(prefix=".installed-", suffix=".tmp", dir=pack)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as out:
            json.dump(receipt, out, indent=2)
            out.write("\n")
            out.flush()
            os.fsync(out.fileno())
        if destination.is_symlink():
            raise ValueError("installed receipt must not be a symbolic link")
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def install(pack, base, check_only=False):
    pack, base = pack.resolve(), base.resolve()
    manifest = json.loads((pack / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or manifest.get("game_build") not in ("90447", "94012"):
        raise ValueError("unsupported pack manifest")
    if (sha256(base / "data/build_manifest.psv") != manifest["source_manifest_sha256"] or
        sha256(base / "data/.build.config") != manifest["base_config_sha256"]):
        raise ValueError("installed base RomFS differs from prepared source")
    for entry in manifest["files"]:
        path = safe_path(pack, entry["path"])
        if path.is_symlink() or path.stat().st_size != entry["size"] or sha256(path) != entry["sha256"]:
            raise ValueError("prepared payload checksum mismatch: " + entry["path"])
    for entry in manifest["base_files"]:
        source = safe_path(base, entry["path"])
        if not source.is_file() or source.stat().st_size != entry["size"]:
            raise ValueError("base resource missing/wrong size: " + entry["path"])
        destination = safe_path(pack, "RomFS/" + entry["path"])
        if destination.exists() or destination.is_symlink():
            raise ValueError("base resource unexpectedly shadows original overlay fallback")
    receipt = {"verified": True, "base_romfs": str(base), "base_resources": len(manifest["base_files"]),
               "overlay_fallback": True, "base_resources_copied": 0,
               "game_started": False, "encoding_on_device": False}
    if not check_only:
        _write_receipt(pack, receipt)
    print(json.dumps(dict(receipt, check_only=check_only)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Verify a prepared overlay and its existing base resources (exFAT supported)")
    parser.add_argument("pack", type=Path)
    parser.add_argument("base_romfs", type=Path)
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    install(args.pack, args.base_romfs, args.check_only)
