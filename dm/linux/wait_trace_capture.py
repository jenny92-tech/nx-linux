#!/usr/bin/env python3
# INPUT: An explicit owned trace instance and an existing D2R process.
# OUTPUT: Bounded scheduler/Mali/phase trace with identity and loss receipts.
# POS: Device-side diagnostic; never starts games or changes clocks/policies.
"""Prepare before launching with DM_LINUX_WAIT_TRACE_FILE, capture after readiness.

prepare OUTPUT_DIRECTORY
capture OUTPUT_DIRECTORY PID --seconds 10
cleanup OUTPUT_DIRECTORY  (after the game's marker descriptor has closed)
"""
import argparse
import json
import os
from pathlib import Path
import re
import time


def write(path, value):
    path.write_text(str(value))


def proc_identity(pid):
    proc = Path('/proc', str(pid))
    exe = os.readlink(proc / 'exe')
    if not Path(exe).name.startswith('d2r-linux-'):
        raise ValueError('capture target must be D2R')
    return dict(pid=pid, exe=exe, start_ticks=(proc / 'stat').read_text().rsplit(')', 1)[1].split()[19])


def metadata(folder):
    meta = json.loads((folder / 'trace-instance.json').read_text())
    instance = Path(meta['instance'])
    if instance.parent not in (Path('/sys/kernel/debug/tracing/instances'), Path('/sys/kernel/tracing/instances')):
        raise ValueError('unexpected trace root')
    if not re.fullmatch(r'd2r-wait-[A-Za-z0-9_-]{1,80}', instance.name):
        raise ValueError('unexpected trace name')
    if Path('/proc/sys/kernel/random/boot_id').read_text().strip() != meta['boot_id']:
        raise ValueError('device rebooted')
    return meta, instance


def prepare(folder):
    if os.geteuid() != 0:
        raise PermissionError('trace instance requires root')
    folder.mkdir(parents=True, exist_ok=False)
    root = next((p for p in (Path('/sys/kernel/debug/tracing'), Path('/sys/kernel/tracing'))
                 if (p / 'available_events').is_file()), None)
    if root is None:
        raise RuntimeError('tracefs events unavailable')
    instance = root / 'instances' / ('d2r-wait-' + str(os.getpid()) + '-' + str(time.monotonic_ns()))
    instance.mkdir()
    try:
        write(instance / 'tracing_on', 0)
        write(instance / 'trace_clock', 'mono')
        write(instance / 'buffer_size_kb', 4096)
        write(instance / 'events/enable', 0)
        meta = dict(instance=str(instance), marker=str(instance / 'trace_marker'),
                    boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
                    clock='mono', buffer_kb_per_cpu=4096, created_monotonic_ns=time.monotonic_ns())
        (folder / 'trace-instance.json').write_text(json.dumps(meta, indent=2) + '\n')
    except BaseException:
        write(instance / 'tracing_on', 0)
        instance.rmdir()
        raise
    return meta


def stats(instance):
    result = {}
    for path in sorted(instance.glob('per_cpu/cpu*/stats')):
        fields = {}
        for line in path.read_text().splitlines():
            key, _, value = line.partition(':')
            if key in ('overrun', 'commit overrun', 'dropped events', 'entries'):
                fields[key] = int(value.strip())
        if not {'overrun', 'commit overrun', 'dropped events', 'entries'} <= fields.keys():
            raise ValueError('missing trace loss counters: ' + str(path))
        result[path.parent.name] = fields
    return result


def capture(folder, pid, seconds):
    if not 1 <= seconds <= 30:
        raise ValueError('capture must last 1..30 seconds')
    meta, instance = metadata(folder)
    target = proc_identity(pid)
    output = folder / ('capture-' + str(time.monotonic_ns()))
    output.mkdir()
    tids = sorted(int(p.name) for p in Path('/proc', str(pid), 'task').iterdir())
    tids_filter = lambda field: '(' + ' || '.join(f'{field} == {tid}' for tid in tids) + ')'
    filters = {
        'sched/sched_switch': tids_filter('prev_pid') + ' || ' + tids_filter('next_pid'),
        'sched/sched_wakeup': tids_filter('pid'),
        'sched/sched_waking': tids_filter('pid'),
        'mali/mali_job_slots_event': f'tgid == {pid}',
    }
    receipt = dict(**meta, target=target, tids=tids, seconds_requested=seconds,
                   filters=filters, complete=False, errors=[])
    enabled = []
    try:
        write(instance / 'tracing_on', 0)
        write(instance / 'events/enable', 0)
        write(instance / 'trace', '')
        for name, condition in filters.items():
            event = instance / 'events' / name
            write(event / 'filter', condition)
            write(event / 'enable', 1)
            enabled.append(event)
            (output / (name.replace('/', '-') + '.format')).write_text((event / 'format').read_text())
        start = time.monotonic()
        write(instance / 'tracing_on', 1)
        write(instance / 'trace_marker', f'dm_capture_begin pid={pid}\n')
        time.sleep(seconds)
        write(instance / 'trace_marker', f'dm_capture_end pid={pid}\n')
        write(instance / 'tracing_on', 0)
        receipt['elapsed_s'] = time.monotonic() - start
        receipt['identity_unchanged'] = proc_identity(pid) == target
        receipt['tids_after'] = sorted(int(p.name) for p in Path('/proc', str(pid), 'task').iterdir())
    except BaseException as error:
        receipt['errors'].append(str(error))
        raise
    finally:
        write(instance / 'tracing_on', 0)
        for event in enabled:
            write(event / 'enable', 0)
        # Keep the trace even when the target exits or an identity check fails.
        try:
            (output / 'trace.txt').write_text((instance / 'trace').read_text())
            receipt['stats'] = stats(instance)
            receipt['lost'] = sum(v[k] for v in receipt['stats'].values()
                                  for k in ('overrun', 'commit overrun', 'dropped events'))
            receipt['complete'] = (bool(receipt['stats']) and not receipt['errors'] and
                                   receipt.get('identity_unchanged', False) and receipt['lost'] == 0 and
                                   receipt.get('tids_after') == tids)
        except (OSError, ValueError) as error:
            receipt['errors'].append('trace preservation: ' + str(error))
        (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return dict(output=str(output), **receipt)


def cleanup(folder):
    meta, instance = metadata(folder)
    write(instance / 'tracing_on', 0)
    write(instance / 'events/enable', 0)
    instance.rmdir()
    (folder / 'cleanup.json').write_text(json.dumps(dict(instance=meta['instance'], removed=True)) + '\n')
    return dict(removed=True, instance=meta['instance'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'capture', 'cleanup'])
    parser.add_argument('directory', type=Path)
    parser.add_argument('pid', type=int, nargs='?')
    parser.add_argument('--seconds', type=float, default=10)
    args = parser.parse_args()
    if args.action == 'prepare':
        result = prepare(args.directory)
    elif args.action == 'cleanup':
        result = cleanup(args.directory)
    else:
        if args.pid is None:
            parser.error('capture requires PID')
        result = capture(args.directory, args.pid, args.seconds)
    print(json.dumps(result))


if __name__ == '__main__':
    main()
