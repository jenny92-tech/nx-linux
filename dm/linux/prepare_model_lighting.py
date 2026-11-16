#!/usr/bin/env python3
# INPUT: Verified private shader bank/inventory and existing SPIRV-Tools.
# OUTPUT: Separate albedo-lighting bank with original code identities and receipts.
# POS: Computer-only low-quality 3D shader experiment; no runtime or save edits.
"""Prepare simpler lighting for reviewed model, alpha-test and foliage fragments.

Retain original UV computation, texture sample and alpha output. Keep unsupported
variants unchanged and report each refusal. This intentionally changes lighting;
the generated bank needs visual and performance validation on the target device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

from optimize_shaders import interface
from prepare_handheld import shader_index_keys
from spirv_model_lighting import albedo_lighting

FAMILIES = {'data:data/hd/shaders/' + name for name in
            ('ps_models_p.bin', 'ps_alpha_threshold_p.bin', 'ps_foliage_p.bin')}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def trim_bindings(metadata, active):
    h = list(struct.unpack_from('<12I', metadata))
    bindings = [struct.unpack_from('<8i', metadata, 48+i*32) for i in range(h[3])]
    original = {b[:2] for b in bindings}
    if not active <= original:
        raise ValueError('replacement introduced a resource binding')
    kept = [b for b in bindings if b[:2] in active]
    tail = metadata[48+h[3]*32:]
    h[3] = len(kept)
    return struct.pack('<12I', *h) + b''.join(struct.pack('<8i', *b) for b in kept) + tail


def run_tool(argv, key):
    result = subprocess.run([str(v) for v in argv], capture_output=True, text=True)
    if result.returncode:
        raise ValueError('shader tool failed for ' + key + ': ' + result.stdout + result.stderr)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared', type=Path, required=True, help='complete optimize_shaders.py bank')
    p.add_argument('--inventory', type=Path, required=True, help='matching original inventory.json')
    p.add_argument('--out', type=Path, required=True)
    group = p.add_mutually_exclusive_group(required=True)
    group.add_argument('--key', action='append', help='explicit model shader key; repeat for multiple variants')
    group.add_argument('--all-eligible-models', action='store_true')
    p.add_argument('--optimizer', type=Path, required=True)
    p.add_argument('--validator', type=Path, required=True)
    p.add_argument('--mksquashfs', type=Path, required=True)
    a = p.parse_args()
    source, out = a.prepared.resolve(), a.out.resolve()
    if out.exists() or source in out.parents or out in source.parents:
        p.error('choose a new, separate private output directory')
    proof = json.loads((source / 'complete.json').read_text())
    if not proof.get('complete') or proof.get('model_lighting_policy'):
        p.error('requires a complete source bank before model lighting changes')
    report = proof['shader_report']
    index = (source / 'shaders/shader_index.bin').read_bytes()
    if hashlib.sha256(index).hexdigest() != report['index_sha256']:
        p.error('shader index provenance mismatch')
    keys = shader_index_keys(index)
    records = {r['key']: r for r in report['records']}
    if keys.keys() != records.keys():
        p.error('source index/record inventory mismatch')
    inventory_raw = a.inventory.read_bytes()
    inventory = json.loads(inventory_raw)
    models = {r['key'] for r in inventory['occurrences'] if r['source'] in FAMILIES}
    selected = sorted(models & keys.keys()) if a.all_eligible_models else sorted(set(a.key))
    if not selected or any(key not in models or keys.get(key) != 1 for key in selected):
        p.error('selected keys must be inventoried reviewed model/foliage fragments in this bank')
    originals = {r['key']: r for r in inventory['programs']}
    at = 12
    while at < len(index):
        key = index[at:at+16].decode()
        size = struct.unpack_from('<I', index, at+20)[0]
        original = index[at+24:at+24+0x880+size]
        if key in selected and hashlib.sha256(original).hexdigest() != originals[key]['full_sha256']:
            p.error('original full-code identity differs from inventory: ' + key)
        at += 24+0x880+size
    out.mkdir(parents=True, mode=0o700)
    shaders, work = out / 'shaders', out / 'work'
    shaders.mkdir(); work.mkdir()
    (shaders / 'shader_index.bin').write_bytes(index)
    changed, declined, output_records, payload_bytes = [], [], [], 0
    for key in sorted(keys):
        record = records[key]
        spv, meta = source / 'shaders' / (key+'.spv'), source / 'shaders' / (key+'.vkmeta')
        if digest(spv) != record['spirv_sha256'] or digest(meta) != record['metadata_sha256']:
            raise ValueError('source payload checksum mismatch: ' + key)
        raw, metadata = spv.read_bytes(), meta.read_bytes()
        if key in selected:
            try:
                variant, evidence = albedo_lighting(raw, metadata)
            except ValueError as error:
                if not a.all_eligible_models:
                    raise
                declined.append(dict(key=key, original_kept=True, reason=str(error)))
            else:
                pending = work / (key+'.spv')
                pending.write_bytes(variant)
                run_tool([a.validator.resolve(), '--target-env', 'vulkan1.1', pending], key)
                destination = shaders / (key+'.spv')
                run_tool([a.optimizer.resolve(), '--target-env=vulkan1.1', '-O',
                    '--preserve-spec-constants', pending, '-o', destination], key)
                run_tool([a.validator.resolve(), '--target-env', 'vulkan1.1', destination], key)
                optimized = destination.read_bytes()
                before, after = interface(raw), interface(optimized)
                if not after[0] <= before[0] or not after[1] <= before[1] or not after[2] <= before[2]:
                    raise ValueError('replacement expanded shader ABI/capabilities: ' + key)
                trimmed = trim_bindings(metadata, after[0])
                source_header = struct.unpack_from('<12I', metadata)
                kept_resources = [struct.unpack_from('<8i', metadata, 48+i*32) for i in range(source_header[3])
                                  if struct.unpack_from('<2i', metadata, 48+i*32) in after[0]]
                (shaders / (key+'.vkmeta')).write_bytes(trimmed)
                output_records.append(dict(record, spirv_sha256=digest(destination),
                    metadata_sha256=hashlib.sha256(trimmed).hexdigest(),
                    model_lighting_policy='albedo-lighting-v1', optimized_bytes=len(optimized),
                    runtime_buffer_slots=[slot for slot in record.get('runtime_buffer_slots', [])
                                          if any(b[3] in (4, 10) and b[4] == slot for b in kept_resources)],
                    source_spirv_sha256=record['spirv_sha256'],
                    source_metadata_sha256=record['metadata_sha256']))
                payload_bytes += len(optimized)
                changed.append(dict(key=key, source_bytes=len(raw), output_bytes=len(optimized),
                    source_bindings=len(before[0]), output_bindings=len(after[0]), **evidence))
                continue
        shutil.copyfile(spv, shaders / spv.name)
        shutil.copyfile(meta, shaders / meta.name)
        output_records.append(record)
        payload_bytes += len(raw)
    if not changed:
        raise ValueError('no reviewed model fragment could be simplified')
    image = out / 'shaders.squashfs'
    with (out / 'squashfs.log').open('w') as log:
        subprocess.run([str(a.mksquashfs.resolve()), str(shaders), str(image), '-noappend',
            '-comp', 'zstd', '-Xcompression-level', '3', '-processors', '2', '-no-progress',
            '-mkfs-time', '0', '-all-root', '-no-xattrs'], check=True, stdout=log)
    result = dict(complete=True, model_lighting_policy='albedo-lighting-v1',
        source_complete_sha256=digest(source/'complete.json'),
        inventory_sha256=hashlib.sha256(inventory_raw).hexdigest(),
        source_scripts_sha256={path.name: digest(path) for path in
            (Path(__file__), Path(__file__).with_name('spirv_model_lighting.py'),
             Path(__file__).with_name('optimize_shaders.py'), Path(__file__).with_name('prepare_handheld.py'))},
        optimizer_sha256=digest(a.optimizer), validator_sha256=digest(a.validator),
        mksquashfs_sha256=digest(a.mksquashfs),
        image_sha256=digest(image), image_bytes=image.stat().st_size,
        shader_report=dict(report, records=output_records, shader_code_changed=True,
                           raw_spirv_bytes=payload_bytes,
                           metadata_change='unused resource bindings removed from selected model fragments'),
        selected=selected, changed=changed, declined=declined,
        original_code_identity_unchanged=True, geometry_and_ui_shaders_unchanged=True,
        source_files_modified=False, runtime_rendering_verified=False, target_fps_verified=False)
    (out/'complete.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(dict(complete=True, changed=len(changed), declined=len(declined),
        source_bytes=sum(r['source_bytes'] for r in changed), output_bytes=sum(r['output_bytes'] for r in changed),
        image=str(image), runtime_rendering_verified=False)))


if __name__ == '__main__':
    main()
