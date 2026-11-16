#!/usr/bin/env python3
# INPUT: A hash-verified private prepare_game.py output and existing host tools.
# OUTPUT: A new validated shader image with explicit strict/optimized policy.
# POS: Computer-only recompilation experiment; never edits source/game/save trees.
"""Recompile shaders without repeating extraction or texture encoding.

upstream matches the author's relaxed-FP policy while retaining DebugMode.
optimized additionally enables the AST optimizer. Translation failures stop
explicitly instead of silently dropping shaders.
GPU rendering comparisons and actual-device FPS are still required.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from prepare_game import check_tree, buffer_inventory, merge_caches, write_json, digest
from prepare_handheld import prepare_shaders

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def translation_policy(name):
    if name == 'strict':
        return dict(debug=True, reduced=False)
    if name == 'upstream':
        return dict(debug=True, reduced=True)
    if name == 'optimized':
        return dict(debug=False, reduced=True)
    raise ValueError('unknown shader policy')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared', type=Path, required=True, help='completed private prepare_game.py output')
    p.add_argument('--out', type=Path, required=True, help='new private output directory')
    p.add_argument('--policy', choices=('strict', 'upstream', 'optimized'), required=True)
    p.add_argument('--dotnet-root', type=Path, required=True)
    p.add_argument('--validator', type=Path, required=True)
    p.add_argument('--mksquashfs', type=Path, required=True)
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--translator', type=Path,
                   help='explicit newly built translator; record its binary/dependency hashes')
    a = p.parse_args()
    source, out = a.prepared.resolve(), a.out.resolve()
    if out.exists() or source in out.parents or not 1 <= a.jobs <= 8:
        p.error('new output directory and jobs 1..8 required')
    if not json.loads((source / 'complete.json').read_text()).get('complete'):
        p.error('source preparation is incomplete')
    # Verify the actual source identities, full original shader programs and
    # embedded translator/distance tools before reusing any private material.
    for stage in ('03-tools', '04-inventory'):
        check_tree(source / stage, json.loads((source / stage / 'stage.json').read_text())['files'])
    translator = a.translator.resolve() if a.translator else source / '03-tools/xlate/dmsh_xlate'
    env = dict(os.environ, DOTNET_ROOT=str(a.dotnet_root.resolve()))
    expected = hashlib.sha256(b''.join((ROOT / 'dm/gfx/tools/dmsh_xlate' / n).read_bytes()
                                      for n in ('Program.cs', 'dmsh_xlate.csproj'))).hexdigest()[:16]
    if subprocess.check_output([str(translator), '--source-sha'], env=env, text=True).strip() != expected:
        p.error('translator embedded source differs from checkout')
    translator_files = {f.name: digest(f) for f in sorted(translator.parent.iterdir())
                        if f.is_file() and (f == translator or f.suffix in ('.dll', '.json'))}
    ryu = ROOT / 'dm/third_party/ryujinx'
    translator_library_sources = {str(f.relative_to(ryu)): digest(f)
        for f in sorted(ryu.rglob('*')) if f.is_file() and f.suffix in ('.cs', '.csproj', '.props', '.targets')
        and not any(part in ('bin', 'obj') for part in f.relative_to(ryu).parts)} if a.translator else None
    out.mkdir(parents=True, mode=0o700)
    (out / 'logs').mkdir()
    policy = translation_policy(a.policy)

    def run(name, command):
        print(name, flush=True)
        with (out / 'logs' / (name + '.log')).open('w') as log:
            subprocess.run([str(c) for c in command], env=env, check=True, stdout=log, stderr=subprocess.STDOUT)

    inventory = out / '01-inventory'
    shutil.copytree(source / '04-inventory', inventory)
    (inventory / 'stage.json').unlink()
    jobs = json.loads((inventory / 'jobs.json').read_text())
    for job in jobs:
        job.update(policy, code=str(inventory / 'code' / (job['key'] + '.bin')))
    write_json(inventory / 'jobs.json', jobs)
    original = json.loads((inventory / 'inventory.json').read_text())
    original['translation_policy'] = dict(name=a.policy, **policy)
    write_json(inventory / 'inventory.json', original)
    translated = out / '02-translated'
    run('translate', [translator, inventory / 'jobs.json', translated])

    def validate(name, inv, spv):
        destination = out / name
        run(name, [sys.executable, HERE / 'shader_cache.py', '--inventory', inv, '--spv', spv,
                   '--validator', a.validator.resolve(), '--distance-tool', source / '03-tools/distance',
                   '--jobs', a.jobs, '--out', destination])
        return destination

    validated = validate('03-validated', inventory, translated)
    variants = out / '04-buffer-inventory'
    buffer_inventory(inventory, translated, validated, variants)
    variant_spv = out / '05-buffer-translated'
    run('buffer-translate', [translator, variants / 'jobs.json', variant_spv])
    variant_cache = validate('06-buffer-validated', variants, variant_spv)
    merged = out / '07-merged'
    merge_caches([validated, variant_cache], inventory, merged)
    report = prepare_shaders(merged, out / 'shaders')
    run('squashfs', [a.mksquashfs.resolve(), out / 'shaders', out / 'shaders.squashfs',
                    '-noappend', '-comp', 'zstd', '-Xcompression-level', '3',
                    '-processors', '2', '-no-progress', '-mkfs-time', '0', '-all-root'])
    write_json(out / 'complete.json', dict(complete=True, policy=dict(name=a.policy, **policy),
        source_complete_sha256=digest(source / 'complete.json'), translator_source=expected,
        translator_files=translator_files, translator_library_sources=translator_library_sources,
        validator_sha256=digest(a.validator), image_sha256=digest(out / 'shaders.squashfs'),
        image_bytes=(out / 'shaders.squashfs').stat().st_size, shader_report=report,
        game_data_or_keys_uploaded=False, runtime_rendering_verified=False, target_fps_verified=False))
    print(json.dumps(dict(complete=True, programs=report['programs'], image=str(out / 'shaders.squashfs'))))


if __name__ == '__main__':
    main()
