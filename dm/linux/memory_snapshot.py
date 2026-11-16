#!/usr/bin/env python3
# INPUT: An identity-pinned Linux session and readable procfs/renderer metadata.
# OUTPUT: Local compressed evidence, a classified memory report and scene diffs.
# POS: Read-only host/device diagnostics; no signals, memory writes or raw heap export.
"""Capture Linux memory ownership evidence and compare snapshots offline."""
from __future__ import annotations

import argparse
from collections import defaultdict
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import time

CLASSES = ('texture', 'sample_copy', 'shadow', 'pool', 'support', 'temp',
           'writeback', 'readback', 'present')
SYMBOLS = {'memory_stats', 'g_heap', 'g_heap_cur', 'g_heap_cap', 'g_as', 'g_reserve'}
METRICS = ('Size', 'Rss', 'Pss', 'Private_Clean', 'Private_Dirty',
           'Shared_Clean', 'Shared_Dirty', 'Anonymous', 'Swap')
HEADER = re.compile(r'^([0-9a-f]+)-([0-9a-f]+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\d+)(?:\s+(.*))?$')


def sha_file(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def elf_symbols(path):
    """Only named data symbols from an ordinary little-endian ELF64 symtab."""
    with Path(path).open('rb') as stream:
        def read(offset, size):
            if size < 0 or size > 64 * 1024 * 1024:
                raise ValueError('ELF section exceeds diagnostic bound')
            stream.seek(offset)
            raw = stream.read(size)
            if len(raw) != size:
                raise ValueError('Truncated ELF')
            return raw
        head = read(0, 64)
        if head[:6] != b'\x7fELF\x02\x01':
            raise ValueError('Requires ELF64 little endian')
        phoff = struct.unpack_from('<Q', head, 32)[0]
        phstride, phcount = struct.unpack_from('<HH', head, 54)
        if phstride != 56 or not 0 < phcount <= 1024:
            raise ValueError('Unsupported ELF program table')
        loads = [struct.unpack('<IIQQQQQQ', read(phoff + i * phstride, 56)) for i in range(phcount)]
        shoff = struct.unpack_from('<Q', head, 40)[0]
        stride, count = struct.unpack_from('<HH', head, 58)
        if stride != 64 or not 0 < count <= 8192:
            raise ValueError('Unsupported ELF section table')
        sections = [struct.unpack('<IIQQQQIIQQ', read(shoff + i * stride, 64)) for i in range(count)]
        found = {}
        for section in sections:
            if section[1] != 2:
                continue
            if section[6] >= count or section[9] != 24 or section[5] % 24:
                raise ValueError('Invalid ELF symbol table')
            strings = sections[section[6]]
            names = read(strings[4], strings[5])
            for name, info, other, index, value, size in struct.iter_unpack('<IBBHQQ', read(section[4], section[5])):
                end = names.find(b'\0', name)
                if end < 0:
                    raise ValueError('Invalid ELF symbol name')
                key = names[name:end].decode(errors='replace')
                if key in SYMBOLS and index and info & 15 == 1:
                    if key in found:
                        raise ValueError('Ambiguous ELF data symbol: ' + key)
                    found[key] = dict(value=value, size=size)
        return dict(elf_type=struct.unpack_from('<H', head, 16)[0], symbols=found,
                    loads=[dict(offset=p[2], vaddr=p[3], file_bytes=p[5]) for p in loads if p[0] == 1])


def elf_load_bias(elf, mapping_start, page_size):
    candidates = {p['vaddr'] // page_size * page_size for p in elf['loads']
                  if p['offset'] < page_size and p['file_bytes']}
    if len(candidates) != 1 or elf['elf_type'] not in (2, 3):
        raise ValueError('Cannot resolve mapped ELF load bias')
    bias = mapping_start - candidates.pop()
    if elf['elf_type'] == 2 and bias != 0:
        raise ValueError('Unexpected ET_EXEC mapping')
    return bias


def parse_smaps(text):
    rows = []
    for line in text.splitlines():
        match = HEADER.match(line)
        if match:
            lo, hi, perm, offset, device, inode, path = match.groups()
            rows.append(dict(start=int(lo, 16), end=int(hi, 16), permissions=perm,
                             offset=int(offset, 16), device=device, inode=int(inode), path=path or ''))
        elif rows and ':' in line:
            key, value = line.split(':', 1)
            fields = value.split()
            if key in METRICS and len(fields) == 2 and fields[1] == 'kB':
                rows[-1][key] = int(fields[0]) * 1024
    if not rows:
        raise ValueError('No smaps mappings')
    return rows


def kb_fields(text):
    return {k: int(v) * 1024 for k, v in re.findall(r'^(\w+):\s+(\d+)\s+kB$', text, re.M)}


def ledger_decode(raw):
    if len(raw) != 304 or struct.unpack_from('<II', raw) != (1, 9):
        raise ValueError('Unsupported renderer memory ledger ABI')
    values = struct.unpack_from('<37Q', raw, 8)
    if sum(values[:9]) != values[27] or values[27] > values[28]:
        raise ValueError('Renderer ledger changed or is inconsistent')
    if any(values[i] > values[i + 9] for i in range(9)):
        raise ValueError('Renderer class exceeds its peak')
    return dict(classes={key: dict(bytes=values[i], peak_bytes=values[i + 9],
                                  allocations=values[i + 18]) for i, key in enumerate(CLASSES)},
                total_bytes=values[27], peak_total_bytes=values[28],
                cpu_baseline_bytes=values[29], cpu_baseline_peak_bytes=values[30],
                trim_count=values[34], snapshot='matching double read, not an allocator lock',
                physical_residency=False, submission_ownership_subtotals_available=False)


def valid_bounds(bounds):
    values = [bounds.get(k, 0) for k in ('g_heap', 'g_heap_cap', 'g_as', 'g_reserve')]
    heap, cap, base, reserve = values
    return all(type(v) is int and v > 0 for v in values) and base <= heap < heap + cap <= base + reserve


def mapping_group(row, bounds):
    known = valid_bounds(bounds)
    for name, base_key, size_key in [('guest_heap', 'g_heap', 'g_heap_cap'),
                                      ('guest_address_space', 'g_as', 'g_reserve')]:
        base, size = bounds.get(base_key, 0), bounds.get(size_key, 0)
        if known and base <= row['start'] < row['end'] <= base + size:
            return name
    path = row['path']
    if '/dev/mali' in path:
        return 'mali_mapping'
    if path == '[heap]':
        return 'host_libc_heap'
    if path.startswith('/'):
        return 'file:' + path
    return 'host_anonymous_or_stack' if known else 'anonymous_unattributed'


def summarize(raw):
    if raw.get('schema') != 1:
        raise ValueError('Unsupported snapshot schema')
    rows = parse_smaps(raw['files']['smaps'])
    bounds = raw.get('renderer', {}).get('globals', {})
    groups = defaultdict(lambda: {k: 0 for k in METRICS})
    for row in rows:
        row['group'] = mapping_group(row, bounds)
        for key in METRICS:
            groups[row['group']][key] += row.get(key, 0)
    totals = {k: sum(row.get(k, 0) for row in rows) for k in METRICS}
    status = kb_fields(raw['files']['status_after'])
    system = kb_fields(raw['files']['meminfo'])
    return dict(schema=1, label=raw['label'], identity=raw['identity'], units='bytes',
                captured_unix_ns=raw['captured_unix_ns'], elapsed_seconds=raw['elapsed_seconds'],
                status=status, system=system, smaps_totals=totals, groups=dict(groups),
                classification_available=valid_bounds(bounds),
                top_mappings=sorted(rows, key=lambda r: r.get('Pss', 0), reverse=True)[:30],
                mali=raw.get('mali'), renderer=raw.get('renderer', {}),
                guest_residency=raw.get('guest_residency'), dma_buf=raw.get('dma_buf', {}),
                status_minus_smaps_rss_bytes=status.get('VmRSS', 0) - totals['Rss'],
                limitations=[
                    'Live observational snapshot; sources are sampled sequentially and can change.',
                    'Vulkan bytes are allocation sizes, not physical residency. Imported aliases may share pages.',
                    'Keep RSS, PSS, Mali and DMA counters separate; do not add them to estimate physical use.',
                    'Collector overhead affects MemAvailable; this is not a clean FPS sample.',
                    'No raw heap contents or resource-to-caller lifetime tracing is collected.'],
                warnings=raw.get('warnings', []))


def identity_check(spec):
    proc = Path('/proc', str(spec['pid']))
    if Path('/proc/sys/kernel/random/boot_id').read_text().strip() != spec['boot_id']:
        raise ValueError('Boot identity changed')
    if os.readlink(proc / 'exe') != spec['exe']:
        raise ValueError('Executable identity changed')
    fields = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
    if fields[0] == 'Z' or fields[19] != str(spec['process_start_ticks']):
        raise ValueError('Process exited or PID was reused')
    return proc


def collect_device(spec, label):
    started = time.monotonic()
    proc = identity_check(spec)
    if sha_file(proc / 'exe') != spec['exe_sha256']:
        raise ValueError('Mapped executable SHA256 differs')
    warnings = []
    files = dict(status_before=(proc / 'status').read_text(), maps=(proc / 'maps').read_text(),
                 smaps=(proc / 'smaps').read_text(), meminfo=Path('/proc/meminfo').read_text())
    def optional(path):
        try:
            return Path(path).read_text()
        except OSError as error:
            warnings.append(str(path) + ': ' + str(error))
            return None
    files['smaps_rollup'] = optional(proc / 'smaps_rollup')
    renderer = {}
    fd = None
    try:
        elf = elf_symbols(proc / 'exe')
        rows = [r for r in parse_smaps(files['smaps']) if r['path'] == spec['exe'] and r['offset'] == 0]
        if len(rows) != 1:
            raise ValueError('Cannot resolve mapped ELF load bias')
        bias = elf_load_bias(elf, rows[0]['start'], os.sysconf('SC_PAGE_SIZE'))
        fd = os.open(proc / 'mem', os.O_RDONLY)
        def read_symbol(name, size):
            symbol = elf['symbols'].get(name)
            if not symbol or symbol['size'] != size:
                raise ValueError('Missing or unsupported data symbol: ' + name)
            a = os.pread(fd, size, bias + symbol['value'])
            b = os.pread(fd, size, bias + symbol['value'])
            if len(a) != size or a != b:
                raise ValueError('Unstable data symbol: ' + name)
            return a
        bounds = {}
        for name in sorted(SYMBOLS - {'memory_stats'}):
            try:
                bounds[name] = struct.unpack('<Q', read_symbol(name, 8))[0]
            except ValueError as error:
                warnings.append(str(error))
        renderer['globals'] = bounds
        for attempt in range(20):
            try:
                renderer['ledger'] = ledger_decode(read_symbol('memory_stats', 304))
                break
            except ValueError as error:
                if attempt == 19:
                    warnings.append(str(error))
                time.sleep(.005)
    except (OSError, ValueError) as error:
        warnings.append('Renderer metadata unavailable: ' + str(error))
    finally:
        if fd is not None:
            os.close(fd)
    residency = None
    bounds = renderer.get('globals', {})
    base, size, cap = (bounds.get(k, 0) for k in ('g_heap', 'g_heap_cur', 'g_heap_cap'))
    page = os.sysconf('SC_PAGE_SIZE')
    if base and base % page == 0 and 0 < size <= cap <= 64 * 1024**3:
        try:
            bins = []
            with (proc / 'pagemap').open('rb') as stream:
                for offset in range(0, size, 4 * 1024**2):
                    length = min(4 * 1024**2, size - offset)
                    pages = (length + page - 1) // page
                    stream.seek((base + offset) // page * 8)
                    entries = stream.read(pages * 8)
                    if len(entries) != pages * 8:
                        raise ValueError('Short pagemap read')
                    values = [v[0] for v in struct.iter_unpack('<Q', entries)]
                    bins.append(dict(offset=offset, bytes=length,
                                     present_bytes=sum(bool(v & (1 << 63)) for v in values) * page,
                                     swapped_bytes=sum(bool(v & (1 << 62)) for v in values) * page))
            residency = dict(base=base, logical_bytes=size, page_size=page, bins=bins,
                             present_bytes=sum(b['present_bytes'] for b in bins),
                             scope='guest heap virtual pages; aliases are not deduplicated physical pages')
        except (OSError, ValueError) as error:
            warnings.append('Pagemap unavailable: ' + str(error))
    mali_text = optional('/sys/kernel/debug/mali0/gpu_memory')
    mali = None
    if mali_text is not None:
        matched = [line for line in mali_text.splitlines() if line.split() and
                   line.split()[0].startswith('kctx-') and len(line.split()) >= 3 and
                   line.split()[-2] == str(spec['pid'])]
        mali = dict(context_bytes=sum(int(line.split()[-1]) * page for line in matched) if matched else None,
                    context_lines=matched, parser='RK Mali kctx PID pages', no_matching_context=not bool(matched))
    dma = dict(owned_fdinfo=[])
    for info in sorted((proc / 'fdinfo').iterdir()):
        try:
            text = info.read_text()
            if re.search(r'^(exp_name|size|count):', text, re.M):
                dma['owned_fdinfo'].append(dict(fd=int(info.name), text=text))
        except OSError:
            pass
    dma['system_debug_bufinfo'] = optional('/sys/kernel/debug/dma_buf/bufinfo')
    run = spec.get('run')
    if run:
        try:
            if int(Path(run, 'pid').read_text()) != spec['pid']:
                raise ValueError('Run PID differs')
            with Path(run, 'stdout.log').open('rb') as stream:
                stream.seek(max(0, stream.seek(0, 2) - 256 * 1024))
                files['run_log_tail'] = stream.read(256 * 1024).decode(errors='replace')
        except OSError as error:
            warnings.append('Run log unavailable: ' + str(error))
    files['status_after'] = (proc / 'status').read_text()
    identity_check(spec)
    return dict(schema=1, label=label, captured_unix_ns=time.time_ns(), identity=spec,
                elapsed_seconds=time.monotonic() - started, files=files,
                renderer=renderer, guest_residency=residency, mali=mali, dma_buf=dma, warnings=warnings)


def diff_reports(before, after):
    if before.get('schema') != 1 or after.get('schema') != 1:
        raise ValueError('Unsupported summary schema')
    def delta(a, b, keys):
        return {k: b.get(k, 0) - a.get(k, 0) for k in keys}
    classified = bool(before.get('classification_available') and after.get('classification_available'))
    groups = {key: delta(before['groups'].get(key, {}), after['groups'].get(key, {}), METRICS)
              for key in sorted(before['groups'].keys() | after['groups'].keys())} if classified else {}
    old = before.get('renderer', {}).get('ledger')
    new = after.get('renderer', {}).get('ledger')
    ledger = {k: new['classes'][k]['bytes'] - old['classes'][k]['bytes'] for k in CLASSES} if old and new else None
    identity_keys = ('pid', 'boot_id', 'process_start_ticks', 'exe_sha256')
    return dict(schema=1, units='bytes; after minus before', before=before['label'], after=after['label'],
                same_process=all(before['identity'][k] == after['identity'][k] for k in identity_keys),
                status=delta(before['status'], after['status'], ('VmRSS', 'VmHWM', 'RssAnon', 'RssFile', 'VmSwap')),
                smaps_totals=delta(before['smaps_totals'], after['smaps_totals'], METRICS), groups=groups,
                group_deltas_available=classified,
                mem_available_delta=after['system'].get('MemAvailable', 0) - before['system'].get('MemAvailable', 0),
                mali_context_delta=(after['mali']['context_bytes'] - before['mali']['context_bytes'])
                    if (before.get('mali') or {}).get('context_bytes') is not None and
                       (after.get('mali') or {}).get('context_bytes') is not None else None,
                vulkan_class_deltas=ledger, causal_savings_proven=False)


def report_text(data, diff=False):
    mib = lambda value: '%.2f' % (value / 1024**2)
    if diff:
        lines = ['# 内存快照差分', '', data['before'] + ' → ' + data['after'], '',
                 '同一进程：' + str(data['same_process']) + '；以下为 after − before，单位 MiB。', '',
                 '| 指标 | 变化 |', '|---|---:|']
        lines += ['| %s | %s |' % (key, mib(value)) for key, value in data['status'].items()]
        lines += ['| smaps PSS | %s |' % mib(data['smaps_totals']['Pss']),
                  '| MemAvailable | %s |' % mib(data['mem_available_delta'])]
        groups = data['groups']
    else:
        lines = ['# 内存诊断快照', '', data['label'], '', '| 指标 | MiB |', '|---|---:|']
        lines += ['| %s | %s |' % (key, mib(data['status'].get(key, 0))) for key in ('VmRSS', 'VmHWM', 'RssAnon', 'RssFile')]
        lines += ['| smaps PSS | %s |' % mib(data['smaps_totals']['Pss']),
                  '| MemAvailable | %s |' % mib(data['system'].get('MemAvailable', 0))]
        if (data.get('mali') or {}).get('context_bytes') is not None:
            lines.append('| Mali context | %s |' % mib(data['mali']['context_bytes']))
        groups = data['groups']
    lines += ['', '| 映射分类 | PSS MiB | RSS MiB |', '|---|---:|---:|']
    lines += ['| %s | %s | %s |' % (key.replace('|', '\\|'), mib(row['Pss']), mib(row['Rss']))
              for key, row in sorted(groups.items(), key=lambda item: abs(item[1]['Pss']), reverse=True)[:20]]
    if diff and not data['group_deltas_available']:
        lines += ['', '至少一份快照缺少 guest 边界；分类差分不可用，不能将缺失分类解释成释放。']
    ledger = data.get('vulkan_class_deltas') if diff else data.get('renderer', {}).get('ledger', {}).get('classes')
    if ledger is not None:
        lines += ['', '| Vulkan 分配分类（非物理驻留） | MiB |', '|---|---:|']
        lines += ['| %s | %s |' % (key, mib(value if diff else value['bytes'])) for key, value in ledger.items()]
    lines += ['', 'RSS、PSS、Mali、DMA 与 Vulkan 分配账口径不同，不能相加。快照为顺序读取，未暂停进程；',
              '诊断开销与场景变化会影响整机余量。本报告不单独证明可回收量或开关的因果收益。', '']
    if not diff and data['warnings']:
        lines += ['缺失或不稳定的数据：', ''] + ['- ' + w for w in data['warnings']] + ['']
    return '\n'.join(lines)


def write_bundle(out, summary, raw=None, diff=False):
    out.mkdir(parents=True, exist_ok=False)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (out / 'REPORT.md').write_text(report_text(summary, diff))
    if raw is not None:
        (out / 'raw.json.gz').write_bytes(gzip.compress(json.dumps(raw).encode(), mtime=0))
    manifest = {p.name: dict(bytes=p.stat().st_size, sha256=sha_file(p)) for p in sorted(out.iterdir())}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    capture = sub.add_parser('capture')
    capture.add_argument('--session', type=Path, required=True)
    capture.add_argument('--serial', required=True)
    capture.add_argument('--adb', default='adb')
    capture.add_argument('--label', required=True)
    capture.add_argument('--out', type=Path, required=True)
    compare = sub.add_parser('diff')
    compare.add_argument('before', type=Path)
    compare.add_argument('after', type=Path)
    compare.add_argument('--out', type=Path, required=True)
    device = sub.add_parser('_device')
    device.add_argument('spec')
    device.add_argument('label')
    args = parser.parse_args()
    if args.command == '_device':
        sys.stdout.buffer.write(gzip.compress(json.dumps(collect_device(json.loads(args.spec), args.label)).encode(), mtime=0))
    elif args.command == 'capture':
        if args.out.exists():
            parser.error('Output already exists; choose a new snapshot directory')
        source = json.loads(args.session.read_text())
        keys = ('pid', 'exe', 'exe_sha256', 'boot_id', 'process_start_ticks')
        spec = {k: source[k] for k in keys}
        if source.get('running') is False or type(spec['pid']) is not int or spec['pid'] <= 0:
            parser.error('Session must identify a running process')
        if source.get('run'):
            spec['run'] = source['run']
        command = shlex.join(['python3', '-c', Path(__file__).read_text(), '_device', json.dumps(spec), args.label])
        result = subprocess.run([args.adb, '-s', args.serial, 'exec-out', command],
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=90)
        if result.returncode:
            raise RuntimeError(result.stderr.decode(errors='replace') + result.stdout[:4096].decode(errors='replace'))
        if not result.stdout.startswith(b'\x1f\x8b'):
            raise RuntimeError('Device did not return a snapshot: ' + result.stdout[:4096].decode(errors='replace'))
        raw = json.loads(gzip.decompress(result.stdout))
        summary = summarize(raw)
        write_bundle(args.out, summary, raw)
        print(json.dumps(dict(output=str(args.out), pid=spec['pid'],
                              renderer_ledger='ledger' in raw['renderer'], warnings=raw['warnings'])))
    else:
        before, after = [json.loads((path / 'summary.json').read_text()) for path in (args.before, args.after)]
        summary = diff_reports(before, after)
        write_bundle(args.out, summary, diff=True)
        print(json.dumps(dict(output=str(args.out), same_process=summary['same_process'])))


if __name__ == '__main__':
    main()
