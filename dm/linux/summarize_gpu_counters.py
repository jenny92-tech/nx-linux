#!/usr/bin/env python3
# INPUT: CSV from the read-only libGPUCounters probe and its sample interval.
# OUTPUT: Raw sample means, weighted cycle ratios and measured traffic rates.
# POS: Host analysis of all GPU clients; does not claim PID-specific attribution.
"""Summarize Mali counters without averaging percentages or counting polls as FPS."""
import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path


def summarize(raw, interval_ms):
    if not 100 <= interval_ms <= 5000:
        raise ValueError('interval must be 100..5000 ms')
    rows = csv.reader(io.StringIO(raw))
    device = next(rows, None)
    if not device or device[:2] != ['gpu', 'cores'] or len(device) != 5 or device[3] != 'bus_width':
        raise ValueError('missing probe GPU metadata')
    if next(rows, None) != ['sample', 'counter_id', 'name', 'value']:
        raise ValueError('unexpected counter CSV header')
    samples = {}
    for row in rows:
        if len(row) != 4:
            raise ValueError('incomplete counter record')
        sample, counter = int(row[0]), int(row[1])
        value = float(row[3])
        if sample < 0 or counter < 0 or not row[2] or not math.isfinite(value):
            raise ValueError('invalid counter value')
        entries = samples.setdefault(sample, {})
        if counter in entries or any(name == row[2] for name, _ in entries.values()):
            raise ValueError('duplicate sample counter')
        entries[counter] = row[2], value
    if not samples or sorted(samples) != list(range(len(samples))):
        raise ValueError('missing/nonconsecutive samples')
    inventory = {k: v[0] for k, v in samples[0].items()}
    if any({k: v[0] for k, v in s.items()} != inventory for s in samples.values()):
        raise ValueError('counter inventory changed or last sample is incomplete')
    totals = {name: sum(s[k][1] for s in samples.values()) for k, name in inventory.items()}

    def ratio(numerator, denominator):
        n, d = totals.get(numerator), totals.get(denominator)
        return n/d if n is not None and n >= 0 and d is not None and d > 0 else None

    duration = len(samples)*interval_ms/1000
    return dict(scope='all GPU clients, including compositor; not PID-specific',
        samples=len(samples), interval_ms=interval_ms, nominal_sample_seconds=duration,
        timing='probe sleep interval; collection/transport overhead is not subtracted',
        gpu=dict(shader_cores=int(device[2]), bus_width_bits=int(device[4])),
        ratios=dict(
            core_active_per_gpu_active=ratio('Programmable core active cycles', 'GPU active cycles'),
            instruction_starvation_per_core_active=ratio('Instruction issue starvation cycles', 'Programmable core active cycles'),
            arithmetic_issue_per_core_active=ratio('Arithmetic unit issue cycles', 'Programmable core active cycles'),
            fragment_queue_per_gpu_active=ratio('Fragment queue active cycles', 'GPU active cycles'),
            nonfragment_queue_per_gpu_active=ratio('Non-fragment queue active cycles', 'GPU active cycles')),
        raw_means={name: value/len(samples) for name, value in sorted(totals.items())},
        rates_per_nominal_second={name: value/duration for name, value in sorted(totals.items())
            if 'bytes' in name and value >= 0 and not any(t in name for t in (' per ', 'percentage'))},
        derived_negative_counters=[name for name, value in sorted(totals.items()) if value < 0],
        caution='Activity includes stalls. Low unit utilization alone does not prove a bandwidth or CPU bottleneck.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('csv', type=Path)
    p.add_argument('--interval-ms', type=int, required=True, help='same interval used by the probe')
    p.add_argument('--out', type=Path, required=True, help='new output file')
    a = p.parse_args()
    raw = a.csv.read_bytes()
    report = summarize(raw.decode(), a.interval_ms)
    report['csv_sha256'] = hashlib.sha256(raw).hexdigest()
    with a.out.open('x') as out:
        json.dump(report, out, indent=2); out.write('\n')
    print(json.dumps({k: report[k] for k in ('samples', 'scope', 'ratios')}, indent=2))


if __name__ == '__main__':
    main()
