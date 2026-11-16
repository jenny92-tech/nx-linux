#!/bin/bash
# INPUT: Local input/output folders and optional already-installed host tool paths.
# OUTPUT: The private classic preparation flow, using the bundled source hierarchy.
# POS: Public desktop entry; no system installation or game/key downloads.
set -eu
root=$(cd -- "$(dirname -- "$0")" && pwd)
if [ -f "$root/preparer/dm/linux/prepare_classic.py" ]; then
    exec python3 "$root/preparer/dm/linux/prepare_classic.py" "$@"
fi
exec python3 "$root/prepare_classic.py" "$@"
