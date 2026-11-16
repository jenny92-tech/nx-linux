#!/usr/bin/env python3
# INPUT:  A locally rewritten, decompressed NSO image and module-relative PCs.
# OUTPUT:  Nearest original SDK exports, with explicit symbol-range membership.
# POS:    Resolve bring-up backtraces without borrowing another title's offsets.
import argparse
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("pc", nargs="+", type=lambda value: int(value, 0))
    args = parser.parse_args()
    data = args.image.read_bytes()
    mod0 = struct.unpack_from("<I", data, 4)[0]
    dynamic = mod0 + struct.unpack_from("<i", data, mod0 + 4)[0]
    tags = {}
    while True:
        tag, value = struct.unpack_from("<qQ", data, dynamic)
        dynamic += 16
        if not tag:
            break
        tags[tag] = value
    exports = []
    for offset in range(tags[6], tags[5], 24):
        name, info, _, section, value, size = struct.unpack_from("<IBBHQQ", data, offset)
        if section and info & 15 == 2:
            start = tags[5] + name
            end = data.index(b"\0", start)
            exports.append((value, data[start:end].decode(errors="replace"), size))
    for pc in args.pc:
        candidates = [entry for entry in exports if entry[0] <= pc]
        if not candidates:
            print(f"{pc:#x}: no preceding export")
            continue
        value, name, size = max(candidates)
        print(f"{pc:#x}: {name}+{pc-value:#x} size={size:#x} inside={pc < value+size}")


if __name__ == "__main__":
    main()
