#!/usr/bin/env python3
# INPUT: A complete identity-pinned wait_trace_capture receipt and trace.
# OUTPUT: Paired phase latency and scheduler attribution, with validity flags.
# POS: Offline causality analysis; running time is not equivalent to useful CPU work.
import argparse
from bisect import bisect_left, bisect_right
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import re

LINE = re.compile(r'^\s*.+-(\d+)\s+(?:\(\s*\d+\)\s+)?\[\d+\]\s+\S+\s+(\d+\.\d+):\s+(\w+):\s*(.*)$')
PAIR_ENDS = {
    'completion_lock_acquired': 'completion_lock_begin',
    'completion_fence_end': 'completion_fence_begin',
    'record_lock_acquired': 'record_lock_begin',
    'slot_wait_end': 'slot_wait_begin',
    'fence_wait_end': 'fence_wait_begin',
    'guest_sync_end': 'guest_sync_begin',
    'acquire_wait_end': 'acquire_wait_begin',
    'submitted': 'submit_begin',
}


def ns(value):
    whole, fraction = value.split('.')
    return int(whole) * 1_000_000_000 + int((fraction + '000000000')[:9])


def parse(text):
    rows = []
    for line in text.splitlines():
        match = LINE.match(line)
        if not match:
            continue
        tid, clock, kind, payload = match.groups()
        row = dict(tid=int(tid), ns=ns(clock), event=kind, payload=payload)
        if kind in ('sched_switch', 'sched_wakeup', 'sched_waking', 'mali_job_slots_event'):
            row['fields'] = dict(re.findall(r'(\w+)=([^\s]+)', payload))
        elif payload.startswith('dm_wait '):
            row['marker'] = dict(re.findall(r'(\w+)=([^\s]+)', payload))
            for name in ('seq', 'epoch', 'ticket', 'value'):
                row['marker'][name] = int(row['marker'][name])
        rows.append(row)
    return sorted(rows, key=lambda r: r['ns'])


def attribute(rows, tid, begin, end):
    """Split complete marker intervals without assuming a sleeping task woke immediately."""
    result = dict(running_ns=0, runnable_ns=0, sleeping_ns=0,
                  unresolved_off_cpu_ns=0, missing_switches=0)
    running, since, wake, previous_state = True, begin, None, ''
    for row in rows:
        if row['ns'] < begin or row['ns'] > end:
            continue
        fields = row.get('fields', {})
        if row['event'] == 'sched_wakeup' and int(fields['pid']) == tid:
            if not running and wake is None:
                wake = row['ns']
        elif row['event'] == 'sched_switch':
            if int(fields['prev_pid']) == tid:
                if not running:
                    result['missing_switches'] += 1
                    result['unresolved_off_cpu_ns'] += row['ns'] - since
                else:
                    result['running_ns'] += row['ns'] - since
                running, since, wake, previous_state = False, row['ns'], None, fields['prev_state']
            if int(fields['next_pid']) == tid:
                if running:
                    result['missing_switches'] += 1
                    result['running_ns'] += row['ns'] - since
                elif previous_state.startswith('R'):
                    result['runnable_ns'] += row['ns'] - since
                elif wake is not None:
                    result['sleeping_ns'] += wake - since
                    result['runnable_ns'] += row['ns'] - wake
                else:
                    result['unresolved_off_cpu_ns'] += row['ns'] - since
                running, since = True, row['ns']
    if running:
        result['running_ns'] += end - since
    else:
        result['missing_switches'] += 1
        result['unresolved_off_cpu_ns'] += end - since
    result['wall_ns'] = end - begin
    return result


def distribution(values):
    values = sorted(values)
    if not values:
        return dict(count=0)
    out = dict(count=len(values), mean_ms=sum(values) / len(values) / 1e6, max_ms=values[-1] / 1e6)
    for name, quantile in [('p50', .50), ('p95', .95), ('p99', .99)]:
        out[name + '_ms'] = values[math.ceil(len(values) * quantile) - 1] / 1e6
    return out


def analyze(text, receipt):
    if not receipt.get('complete') or receipt.get('lost') != 0 or receipt.get('errors'):
        raise ValueError('incomplete or lossy trace receipt')
    rows = parse(text)
    pid = receipt['target']['pid']
    capture = [r for r in rows if r['payload'].startswith(('dm_capture_begin ', 'dm_capture_end '))]
    if len(capture) != 2 or capture[0]['payload'] != f'dm_capture_begin pid={pid}' or capture[1]['payload'] != f'dm_capture_end pid={pid}':
        raise ValueError('missing or mismatched capture boundaries')
    rows = [r for r in rows if capture[0]['ns'] <= r['ns'] <= capture[1]['ns']]
    markers = [r for r in rows if 'marker' in r]
    if not markers:
        raise ValueError('no application phase markers')
    sequences = [r['marker']['seq'] for r in markers]
    if len(set(sequences)) != len(sequences) or max(sequences) - min(sequences) + 1 != len(sequences):
        raise ValueError('missing or duplicate application marker sequence')
    pending, samples, edge_ends = {}, defaultdict(list), Counter()
    schedule = defaultdict(list)
    for row in rows:
        fields = row.get('fields', {})
        if row['event'] == 'sched_switch':
            for tid in {int(fields['prev_pid']), int(fields['next_pid'])}:
                schedule[tid].append(row)
        elif row['event'] == 'sched_wakeup':
            schedule[int(fields['pid'])].append(row)
    clocks = {tid: [r['ns'] for r in group] for tid, group in schedule.items()}
    def add_pair(begin, row, name):
        marker = row['marker']
        times = clocks.get(row['tid'], [])
        events = schedule[row['tid']][bisect_left(times, begin['ns']):bisect_right(times, row['ns'])]
        samples[name].append(dict(tid=row['tid'], epoch=marker['epoch'], ticket=marker['ticket'],
                                  value=marker['value'], begin_ns=begin['ns'], end_ns=row['ns'],
                                  **attribute(events, row['tid'], begin['ns'], row['ns'])))
    for row in markers:
        marker = row['marker']; kind = marker['kind']
        if kind in PAIR_ENDS:
            begin_kind = PAIR_ENDS[kind]
            key = (row['tid'], begin_kind)
            begin = pending.pop(key, None)
            if begin is None:
                edge_ends[kind] += 1
                continue
            # Lock acquire starts before a context is protected, so its begin has no epoch/ticket.
            if begin_kind != 'record_lock_begin' and any(begin['marker'][k] != marker[k] for k in ('epoch', 'ticket')):
                raise ValueError('phase pair identifier mismatch')
            add_pair(begin, row, begin_kind.removesuffix('_begin'))
        elif kind in PAIR_ENDS.values():
            key = (row['tid'], kind)
            if key in pending:
                raise ValueError('nested or missing phase end')
            pending[key] = row
    recording, record_begins = {}, []
    for row in markers:
        marker = row['marker']; key = (row['tid'], marker['epoch'])
        if marker['kind'] == 'record_begin':
            recording[key] = row
            record_begins.append(row['ns'])
        elif marker['kind'] == 'submit_begin' and key in recording:
            add_pair(recording.pop(key), row, 'present_record_work' if marker['value'] else 'render_record_work')
    render_completion = [s for s in samples.get('completion_lock', []) if s['value'] == 0]
    overtaken = sum(bisect_left(record_begins, s['end_ns']) > bisect_right(record_begins, s['begin_ns'])
                    for s in render_completion)
    summaries = {}
    for kind, group in samples.items():
        summaries[kind] = dict(wall=distribution([s['wall_ns'] for s in group]),
                               total_ms={k.removesuffix('_ns'): sum(s[k] for s in group) / 1e6
                                         for k in ('running_ns', 'runnable_ns', 'sleeping_ns', 'unresolved_off_cpu_ns')},
                               missing_switches=sum(s['missing_switches'] for s in group))
    mali = Counter(r['fields']['event'] for r in rows if r['event'] == 'mali_job_slots_event')
    return dict(valid=bool(summaries) and all(s['missing_switches'] == 0 for s in summaries.values()),
                duration_s=(capture[1]['ns'] - capture[0]['ns']) / 1e9,
                marker_count=len(markers), phase_summaries=summaries, samples=dict(samples),
                render_completions=len(render_completion),
                new_recordings_while_render_completion_waited=overtaken,
                unmatched_edge_ends=dict(edge_ends), unmatched_edge_begins=len(pending),
                mali_raw_event_counts=dict(mali), mali_event_semantics_verified=False,
                notes=['Phase intervals overlap; do not add them into a frame time.',
                       'Fence end is CPU observation, not calibrated hardware completion.',
                       'Running time includes userspace/kernel work and spinning, not only useful work.'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('capture', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    result = analyze((a.capture / 'trace.txt').read_text(), json.loads((a.capture / 'receipt.json').read_text()))
    a.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'samples'}))


if __name__ == '__main__':
    main()
