#!/usr/bin/env python3
# INPUT: Complete hash-verified raw Shader bank and existing host validation tools.
# OUTPUT: Separate validated NaN-compatibility bank; metadata/index stay identical.
# POS: Optional offline stage before optimization/display effects; no deployment.
"""Prepare an experimental Maxwell min/max/clamp compatibility Shader bank."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from prepare_handheld import shader_index_keys
from spirv_nan_minmax import POLICY, rewrite_nan_minmax


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(source, out, validator, mksquashfs, jobs=2):
    source, out = source.resolve(), out.resolve()
    if out.exists() or source in out.parents or out in source.parents or not 1 <= jobs <= 8:
        raise ValueError('new separate output and jobs 1..8 required')
    source_receipt = (source/'complete.json').read_bytes()
    proof = json.loads(source_receipt)
    if not proof.get('complete') or proof.get('nan_minmax_policy') or proof.get('classic_display_policy'):
        raise ValueError('complete source before NaN/display passes required')
    report = proof['shader_report']
    index = source/'shaders/shader_index.bin'
    index_bytes = index.read_bytes()
    if index.is_symlink() or hashlib.sha256(index_bytes).hexdigest() != report['index_sha256']:
        raise ValueError('original shader index provenance mismatch')
    keys = shader_index_keys(index_bytes)
    records = {r['key']: r for r in report['records']}
    if keys.keys() != records.keys() or len(records) != len(report['records']):
        raise ValueError('shader inventory mismatch')
    out.mkdir(parents=True, mode=0o700)
    bank = out/'shaders'
    bank.mkdir()
    (bank/index.name).write_bytes(index_bytes)

    def one(key):
        record = records[key]
        src, meta = (source/'shaders'/(key+suffix) for suffix in ('.spv', '.vkmeta'))
        if src.is_symlink() or meta.is_symlink():
            raise ValueError('regular source shader files required: '+key)
        raw, metadata = src.read_bytes(), meta.read_bytes()
        if (hashlib.sha256(raw).hexdigest() != record['spirv_sha256'] or
                hashlib.sha256(metadata).hexdigest() != record['metadata_sha256'] or
                record.get('stage') != keys[key]):
            raise ValueError('source shader provenance mismatch: '+key)
        if len(metadata) < 48:
            raise ValueError('truncated shader metadata: '+key)
        h = struct.unpack_from('<12I', metadata)
        if (h[:3] != (0x4d4b5644, 1, keys[key]) or h[11] or h[3] > 64 or h[4] > 32 or
                h[5] > 4096 or h[6] > 32 or
                len(metadata) != 48+h[3]*32+h[4]*8+h[6]*4+h[5]):
            raise ValueError('verified raw-file metadata required: '+key)
        updated, counts = rewrite_nan_minmax(raw)
        target = bank/src.name
        target.write_bytes(updated)
        # Validate every module, including unchanged inputs; a matching checksum
        # is provenance, not a substitute for a structural validity check.
        result = subprocess.run([str(validator.resolve()), '--target-env', 'vulkan1.1', str(target)],
                                capture_output=True, text=True)
        if result.returncode:
            raise ValueError('SPIR-V validation failed: '+key+': '+result.stderr)
        (bank/meta.name).write_bytes(metadata)
        return dict(record, source_spirv_sha256=record['spirv_sha256'],
                    spirv_sha256=digest(target), source_bytes=len(raw), optimized_bytes=len(updated),
                    nan_minmax_counts=counts)

    rows = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for row in pool.map(one, sorted(keys)):
            rows.append(row)
            if len(rows) % 500 == 0:
                print('NaN pass validated', len(rows), '/', len(keys), flush=True)
    image = out/'shaders.squashfs'
    with (out/'squashfs.log').open('w') as log:
        subprocess.run([str(mksquashfs.resolve()), str(bank), str(image), '-noappend', '-comp', 'zstd',
            '-Xcompression-level', '3', '-processors', '2', '-no-progress', '-mkfs-time', '0',
            '-all-root', '-no-xattrs'], check=True, stdout=log)
    updated_report = dict(report, records=rows, shader_code_changed=True, metadata_change='none',
                          raw_spirv_bytes=sum(r['optimized_bytes'] for r in rows))
    result = dict(proof, source_complete_sha256=hashlib.sha256(source_receipt).hexdigest(),
        shader_report=updated_report, nan_minmax_policy=POLICY,
        nan_minmax_counts={name: sum(r['nan_minmax_counts'][name] for r in rows)
                           for name in ('FMin', 'FMax', 'FClamp')},
        nan_minmax_modules=sum(any(r['nan_minmax_counts'].values()) for r in rows),
        nan_minmax_source_already_optimized=bool(proof.get('optimization_flags')),
        optimized_spirv_bytes=updated_report['raw_spirv_bytes'],
        original_code_identity_unchanged=True, metadata_unchanged=True,
        image_bytes=image.stat().st_size, image_sha256=digest(image),
        validator_sha256=digest(validator), mksquashfs_sha256=digest(mksquashfs),
        tool_source_sha256={q.name: digest(q) for q in
            (Path(__file__), Path(__file__).with_name('spirv_nan_minmax.py'))},
        runtime_rendering_verified=False, target_fps_verified=False)
    (out/'complete.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prepared', 'out', 'validator', 'mksquashfs'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    result = prepare(args.prepared, args.out, args.validator, args.mksquashfs, args.jobs)
    print(json.dumps({k: result[k] for k in ('complete', 'nan_minmax_policy', 'nan_minmax_counts',
        'nan_minmax_modules', 'image_sha256', 'runtime_rendering_verified')}, indent=2))


if __name__ == '__main__':
    main()
