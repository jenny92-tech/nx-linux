#!/usr/bin/env python3
# INPUT: Completed private shader preparation and explicit host SPIRV-Tools.
# OUTPUT: Validated optimized SPIR-V image, preserving original identities/ABI.
# POS: Computer-only optimization; no game/save edits or automatic deployment.
"""Optimize an existing shader image without rerunning the Maxwell AST optimizer.

Keep descriptor bindings, specialization constants, original-code index and
runtime type guards. Reject new capabilities or changed descriptor identities.
GPU image correctness and actual FPS must still be checked on the target.
"""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

from prepare_handheld import shader_index_keys
from spirv_c1_constants import bake_c1
from spirv_support_constants import bake_linux_support, CONTRACT as SUPPORT_CONTRACT
from spirv_precision import relax_color_arithmetic


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def interface(raw):
    if len(raw) < 20 or len(raw) % 4:
        raise ValueError('invalid SPIR-V size')
    words = struct.unpack(f'<{len(raw)//4}I', raw)
    if words[0] != 0x07230203:
        raise ValueError('invalid SPIR-V magic')
    bindings, sets, specs, caps = {}, {}, set(), set()
    at = 5
    while at < len(words):
        count, op = words[at] >> 16, words[at] & 65535
        if not count or at + count > len(words):
            raise ValueError('truncated SPIR-V instruction')
        p = words[at+1:at+count]
        if op == 17:
            caps.add(p[0])
        elif op == 71 and len(p) >= 3:
            if p[1] == 33: bindings[p[0]] = p[2]
            if p[1] == 34: sets[p[0]] = p[2]
            if p[1] == 1: specs.add(p[2])
        at += count
    return set((sets[k], v) for k, v in bindings.items()), specs, caps


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', type=Path, required=True,
                        help='completed recompile_shaders.py or optimize_shaders.py directory')
    parser.add_argument('--out', type=Path, required=True, help='new private directory')
    parser.add_argument('--optimizer', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--mksquashfs', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--bake-c1', action='store_true', help='fold proved static reads of the immutable C1 UBO')
    parser.add_argument('--bake-linux-support-v1', action='store_true',
                        help='Linux Vulkan only: fold the renderer-owned immutable support UBO; do not use on Android/iOS')
    parser.add_argument('--support-stages', choices=('fragment', 'graphics'), default='fragment',
                        help='support folding scope; graphics also includes vertex shaders (device validation required)')
    parser.add_argument('--fragment-half', action='store_true',
                        help='EXPERIMENTAL quality reduction: fragment arithmetic to FP16; requires DM_SHADER_FLOAT16=1')
    parser.add_argument('--color-half', action='store_true',
                        help='EXPERIMENTAL FP16 subset retaining control/address/texture-coordinate dependencies at FP32')
    a = parser.parse_args()
    source, out = a.prepared.resolve(), a.out.resolve()
    if out.exists() or source in out.parents or not 1 <= a.jobs <= 8 or a.fragment_half and a.color_half:
        parser.error('new output and jobs 1..8 required')
    if a.support_stages != 'fragment' and not a.bake_linux_support_v1:
        parser.error('--support-stages graphics requires --bake-linux-support-v1')
    tool_sources = {p.name: digest(p) for p in
        (Path(__file__), Path(__file__).with_name('spirv_c1_constants.py'),
         Path(__file__).with_name('spirv_precision.py'),Path(__file__).with_name('spirv_support_constants.py'))}
    proof = json.loads((source / 'complete.json').read_text())
    if not proof.get('complete'):
        parser.error('source preparation incomplete')
    if proof.get('support_contract') not in (None, SUPPORT_CONTRACT):
        parser.error('source has an unknown support-buffer contract')
    report = proof['shader_report']
    index = (source / 'shaders/shader_index.bin').read_bytes()
    if hashlib.sha256(index).hexdigest() != report['index_sha256']:
        parser.error('original-code index provenance mismatch')
    keys = shader_index_keys(index)
    records = {r['key']: r for r in report['records']}
    if keys.keys() != records.keys():
        parser.error('shader inventory mismatch')
    out.mkdir(parents=True, mode=0o700)
    shaders = out / 'shaders'
    shaders.mkdir()
    if a.bake_c1 or a.color_half or a.bake_linux_support_v1: (out / 'work').mkdir()
    (shaders / 'shader_index.bin').write_bytes(index)
    flags = ['--target-env=vulkan1.1', '-O', '--preserve-bindings', '--preserve-spec-constants']

    def one(key):
        record = records[key]
        src = source / 'shaders' / (key + '.spv')
        metadata = source / 'shaders' / (key + '.vkmeta')
        if digest(src) != record['spirv_sha256'] or digest(metadata) != record['metadata_sha256']:
            raise ValueError('source payload provenance mismatch: ' + key)
        target = shaders / (key + '.spv')
        original = src.read_bytes()
        fragment_half = a.fragment_half and struct.unpack_from('<I', metadata.read_bytes(), 8)[0] == 1
        color_half = a.color_half and struct.unpack_from('<I', metadata.read_bytes(), 8)[0] == 1
        folded = support_folded = 0
        prepared, color_ops = original, 0
        if a.bake_c1: prepared, folded = bake_c1(prepared, metadata.read_bytes())
        if a.bake_linux_support_v1:
            prepared, support_folded = bake_linux_support(prepared, metadata.read_bytes(),
                                                         include_vertex=a.support_stages == 'graphics')
        if color_half: prepared, color_ops = relax_color_arithmetic(prepared)
        if a.bake_c1 or color_half or a.bake_linux_support_v1:
            src = out / 'work' / (key + '.spv'); src.write_bytes(prepared)
        stage_flags = flags + (['--relax-float-ops', '--convert-relaxed-to-half', '-O'] if fragment_half else [])
        if color_half: stage_flags += ['--convert-relaxed-to-half', '-O']
        result = subprocess.run([str(a.optimizer.resolve()), *stage_flags, str(src), '-o', str(target)],
                                capture_output=True, text=True)
        if result.returncode:
            raise ValueError('optimizer failed: ' + key + ': ' + result.stderr)
        result = subprocess.run([str(a.validator.resolve()), '--target-env', 'vulkan1.1', str(target)],
                                capture_output=True, text=True)
        if result.returncode:
            raise ValueError('validation failed: ' + key + ': ' + result.stderr)
        raw = target.read_bytes()
        old, new = interface(original), interface(raw)
        allowed_caps = old[2] | ({9} if fragment_half or color_half else set())
        if old[:2] != new[:2] or not new[2] <= allowed_caps:
            raise ValueError('optimized interface/capabilities changed: ' + key)
        # Raw-file metadata has no embedded gzip size. Keep every resource
        # binding/type guard and attribute byte identical to the verified input.
        meta = metadata.read_bytes()
        if struct.unpack_from('<I', meta, 44)[0] != 0:
            raise ValueError('requires pre-inflated raw-file metadata')
        shutil.copyfile(metadata, shaders / metadata.name)
        return dict(record, source_spirv_sha256=record['spirv_sha256'],
                    spirv_sha256=hashlib.sha256(raw).hexdigest(),
                    source_bytes=len(original), optimized_bytes=len(raw), immutable_c1_loads_folded=folded,
                    immutable_support_loads_folded=support_folded,
                    fragment_half=fragment_half, color_half=color_half, color_arithmetic_selected=color_ops,
                    float16_capability=9 in new[2])

    rows = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for row in pool.map(one, sorted(keys)):
            rows.append(row)
            if len(rows) % 500 == 0:
                print('optimized and validated', len(rows), '/', len(keys), flush=True)
    image = out / 'shaders.squashfs'
    with (out / 'squashfs.log').open('w') as log:
        subprocess.run([str(a.mksquashfs.resolve()), str(shaders), str(image), '-noappend',
            '-comp', 'zstd', '-Xcompression-level', '3', '-processors', '2', '-no-progress',
            '-mkfs-time', '0', '-all-root', '-no-xattrs'], check=True, stdout=log)
    updated = dict(report, records=rows, raw_spirv_bytes=sum(r['optimized_bytes'] for r in rows),
                   shader_code_changed=True, metadata_change='none', optimization_flags=flags)
    support_stages = set(proof.get('support_stages', ['fragment'] if proof.get('support_contract') else []))
    if a.bake_linux_support_v1:
        support_stages.update(('vertex', 'fragment') if a.support_stages == 'graphics' else ('fragment',))
    result = dict(complete=True, source_complete_sha256=digest(source / 'complete.json'),
        optimizer_sha256=digest(a.optimizer), validator_sha256=digest(a.validator),
        tool_source_sha256=tool_sources, mksquashfs_sha256=digest(a.mksquashfs),
        optimization_flags=flags, image_bytes=image.stat().st_size, image_sha256=digest(image),
        shader_report=updated, source_spirv_bytes=sum(r['source_bytes'] for r in rows),
        optimized_spirv_bytes=updated['raw_spirv_bytes'],
        original_code_identity_unchanged=True, metadata_unchanged=True,
        c1_baking=a.bake_c1, immutable_c1_loads_folded=sum(r['immutable_c1_loads_folded'] for r in rows),
        support_contract=SUPPORT_CONTRACT if a.bake_linux_support_v1 else proof.get('support_contract'),
        support_stages=sorted(support_stages),
        nan_minmax_policy=proof.get('nan_minmax_policy'),
        nan_minmax_counts=proof.get('nan_minmax_counts'),
        nan_minmax_modules=proof.get('nan_minmax_modules',0),
        nan_minmax_source_already_optimized=proof.get('nan_minmax_source_already_optimized',False),
        immutable_support_loads_folded=sum(r['immutable_support_loads_folded'] for r in rows),
        experimental_fragment_half=a.fragment_half,
        experimental_color_half=a.color_half,
        color_arithmetic_selected=sum(r['color_arithmetic_selected'] for r in rows),
        fragment_half_programs=sum(r['fragment_half'] for r in rows),
        float16_programs=sum(r['float16_capability'] for r in rows),
        runtime_rendering_verified=False, target_fps_verified=False)
    (out / 'complete.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'shader_report'}), flush=True)


if __name__ == '__main__':
    main()
