#!/usr/bin/env python3
# INPUT: One D2R perf CSV, sampler receipt, matching process maps/module log/layouts.
# OUTPUT: Exclusive CPU sample shares, thread costs and expanded guest-code counts.
# POS: Offline profiling analysis; sample shares are not wall time or FPS gains.
import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import json
from pathlib import Path
import re


def summarize(rows, maps, modules, layouts):
    images = []
    for line in maps.splitlines():
        f = line.split(maxsplit=5)
        if len(f) < 5 or 'x' not in f[1]:
            continue
        lo, hi = (int(v, 16) for v in f[0].split('-'))
        images.append((lo, hi, f[5] if len(f) == 6 else '[anonymous executable]'))
    guests = []
    for match in re.finditer(r'\[dm\] (\w+): base (0x[0-9a-f]+) size (0x[0-9a-f]+) build_id=([0-9a-f]{32})', modules):
        name, base, size, build = match.groups()
        guests.append((int(base, 16), int(size, 16), name, build))
    if not guests or len({g[2] for g in guests}) != len(guests):
        raise ValueError('matching original module log is required')
    patch = {}
    if layouts:
        for _, _, name, _ in guests:
            text = (layouts / (name + '.fixups.tsv')).read_text()
            match = re.search(r'^# PATCH_OFF\s+(0x[0-9a-f]+)$', text, re.M)
            if not match:
                raise ValueError('missing module patch boundary: ' + name)
            patch[name] = int(match[1], 16)

    def resolve(pc):
        region = next((r for r in images if r[0] <= pc < r[1]), None)
        if not region:
            return '[outside captured executable maps]', pc, False
        for base, size, name, _ in guests:
            if base <= pc < base + size:
                offset = pc - base
                return name, offset, name in patch and offset >= patch[name]
        return region[2], pc - region[0], False

    counts, threads, expanded, pages = Counter(), defaultdict(Counter), Counter(), defaultdict(Counter)
    inclusive = Counter()
    pids = set()
    for row in rows:
        pids.add(int(row['pid']))
        name, offset, rewritten = resolve(int(row['ip'], 16))
        counts[name] += 1
        threads[int(row['tid'])][name] += 1
        pages[name][hex(offset & ~4095)] += 1
        if rewritten:
            expanded[name] += 1
        for image in {resolve(int(v, 16))[0] for v in row.get('callchain', '').split(';') if v}:
            inclusive[image] += 1
    if len(pids) != 1 or not rows:
        raise ValueError('one nonempty PID-scoped recording is required')
    return dict(pid=pids.pop(), samples=len(rows),
        meaning='exclusive shares of sampled CPU time, not elapsed frame time or expected FPS gain',
        exclusive=[dict(image=name, samples=n, percent=100*n/len(rows)) for name, n in counts.most_common()],
        threads={str(t): dict(c) for t, c in threads.items()},
        guest_expanded_code_samples=dict(expanded),
        expanded_code_note='patch regions also contain TLS/SVC/timer/other gates; not every sample is an x18 rewrite',
        inclusive_images=dict(inclusive),
        hot_pages={name: c.most_common(16) for name, c in pages.items()})


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('csv', 'receipt', 'maps', 'modules', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--layouts', type=Path, help='matching private module directory with *.fixups.tsv')
    a = p.parse_args()
    if a.output.exists():
        p.error('choose a new output file')
    receipt = json.loads(a.receipt.read_text().splitlines()[-1])
    if not receipt.get('complete') or receipt.get('lost'):
        p.error('incomplete or lossy sampling cannot establish CPU shares')
    rows = list(csv.DictReader(a.csv.open()))
    result = summarize(rows, a.maps.read_text(), a.modules.read_text(), a.layouts)
    if receipt['samples'] != len(rows) or receipt['pid'] != result['pid']:
        p.error('sampler receipt/CSV identity mismatch')
    result['inputs_sha256'] = {name: hashlib.sha256(getattr(a, name).read_bytes()).hexdigest()
                              for name in ('csv', 'receipt', 'maps', 'modules')}
    a.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(dict(samples=result['samples'], exclusive=result['exclusive']), ensure_ascii=False))


if __name__ == '__main__':
    main()
