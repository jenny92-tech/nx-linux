#!/usr/bin/env python3
# INPUT: Local original 90447 files/keys and explicitly selected host tools.
# OUTPUT: Reproducible private device/game tree with stage hashes and logs.
# POS: End-to-end computer preparation; publish tools/runtime, never this output.
"""Prepare your own game for RK3566, entirely on the computer. See PREPARE.zh-CN.md."""
import argparse
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

from extract_local import digest, extract, verify_update
from handheld_mips import logical_path
from prepare_handheld import shader_index_keys

ROOT = Path(__file__).resolve().parents[2]
LINUX = ROOT / 'dm/linux'
QUERY = '贴图句柄 6A（常量段 -1，槽 8）无 PC 类型，按 2D'
QUERY43 = '贴图句柄 B0（常量段 -1，槽 43）无 PC 类型，按 2D'
# The tested experimental runtime excludes these four complete original-code
# identities. Slot 43's type is unknown: never manufacture a successful answer.
KNOWN_EXCLUDED = {
    '7765db6d93c5300b': '7765db6d93c5300b4b5b2e576fb383853530119cf0ab2b09ffa429eba0f14dc3',
    '8faa4de13cafb808': '8faa4de13cafb80873f18132edd7048ddce3714fd0801cb82040c1d298c9f330',
    '7f4cff5fc41c5249': '7f4cff5fc41c5249a179a6938c6c513c4f581eab7b008a5bb1d734e18c6bbc10',
    '918b1b5e52872f2d': '918b1b5e52872f2d2877a19c0747ca27ead742c2f7cd86223592439c07bca631',
}


def known_exclusions(programs):
    result = []
    for program in programs:
        key = program['key']
        if key in KNOWN_EXCLUDED:
            if program.get('full_sha256') != KNOWN_EXCLUDED[key]:
                raise ValueError('excluded shader full identity mismatch')
            result.append(dict(key=key, full_sha256=KNOWN_EXCLUDED[key],
                               reason='unverified slot 43 texture type; not exported'))
    return result


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def tree_files(root):
    return {p.relative_to(root).as_posix(): dict(size=p.stat().st_size, sha256=digest(p))
            for p in sorted(root.rglob('*')) if p.is_file() and p.name != 'stage.json'}


def check_tree(root, files):
    if tree_files(root) != files:
        raise ValueError('stage output changed or is incomplete: ' + str(root))


class Pipeline:
    def __init__(self, root, resume, identity):
        self.root = root
        if root.exists():
            if not resume or not (root / 'pipeline.json').is_file():
                raise ValueError('use a new --out, or --resume on this tool\'s existing output')
            if json.loads((root / 'pipeline.json').read_text()) != identity:
                raise ValueError('input/tool/source changed; use a new output directory')
        else:
            root.mkdir(parents=True)
            root.chmod(0o700)
            write_json(root / 'pipeline.json', identity)
        (root / 'logs').mkdir(exist_ok=True)

    def stage(self, name, action):
        path = self.root / name
        receipt = path / 'stage.json'
        if receipt.is_file():
            check_tree(path, json.loads(receipt.read_text())['files'])
            print('verified reuse: ' + name, flush=True)
            return path
        if path.exists():
            raise ValueError(f'incomplete stage: {path}; move/remove only that stage directory, then --resume')
        print('prepare: ' + name, flush=True)
        action(path)
        write_json(receipt, dict(complete=True, files=tree_files(path)))
        return path

    def run(self, name, argv, env=None):
        print('  ' + name + ' (see logs/' + name + '.log)', flush=True)
        with (self.root / 'logs' / (name + '.log')).open('w') as log:
            result = subprocess.run([str(a) for a in argv], cwd=ROOT, env=env,
                                    stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise ValueError(f'{name} failed ({result.returncode}); inspect logs/{name}.log')


def buffer_inventory(inventory, spv, validation, output):
    """Conditional BUFFER variant, never a global guessed texture-type fact."""
    full = json.loads((inventory / 'inventory.json').read_text())
    jobs = {j['key']: j for j in json.loads((inventory / 'jobs.json').read_text())}
    programs = {p['key']: p for p in full['programs']}
    selected, rows = [], []
    exclusions = known_exclusions(full['programs'])
    excluded_keys = {row['key'] for row in exclusions}
    for row in json.loads((validation / 'validation.json').read_text())['records']:
        if row['validated']:
            continue
        key = row['key']
        reflection = json.loads((spv / (key + '.res.json')).read_text(encoding='utf-8-sig'))
        if key in excluded_keys and set(reflection['notes']) == {QUERY, QUERY43}:
            continue
        if set(reflection['notes']) != {QUERY}:
            raise ValueError('unreviewed shader validation failure: ' + key + ': ' + row.get('error', ''))
        job = dict(jobs[key])
        if '8' in job.get('tex', {}):
            raise ValueError('conditional texture specialization would override an existing fact')
        job['tex'] = dict(job.get('tex', {}), **{'8': 'buffer'})
        job['runtime_buffer_slots'] = [8]
        selected.append(job)
        rows.append(programs[key])
    output.mkdir()
    (output / 'original').mkdir()
    for job in selected:
        shutil.copyfile(inventory / 'original' / (job['key'] + '.bin'),
                        output / 'original' / (job['key'] + '.bin'))
    write_json(output / 'jobs.json', selected)
    write_json(output / 'inventory.json', dict(programs=rows,
        source_inventory_sha256=digest(inventory / 'inventory.json'),
        conditional_specialization='slot 8 must be NVN BUFFER; runtime rejects other targets/views',
        texture_types_globally_verified=False, excluded=exclusions))
    return len(selected)


def merge_caches(sources, inventory, output):
    output.mkdir()
    entries, records, receipts = {}, {}, []
    for source in sources:
        proof = json.loads((source / 'validation.json').read_text())
        data = (source / 'shader_index.bin').read_bytes()
        if hashlib.sha256(data).hexdigest() != proof['index_sha256']:
            raise ValueError('shader cache index provenance')
        keys = shader_index_keys(data)
        rows = {r['key']: r for r in proof['records'] if r['validated']}
        at = 12
        for key in keys:
            size = struct.unpack_from('<I', data, at + 20)[0]
            end = at + 24 + 0x880 + size
            entry = data[at:end]
            if hashlib.sha256(entry[24:]).hexdigest()[:16] != key:
                raise ValueError('full shader identity mismatch')
            if key in entries and entries[key] != entry:
                raise ValueError('shader identity collision')
            for suffix, field in (('.vkmeta', 'metadata_sha256'), ('.spv.gz', 'gzip_sha256')):
                raw = (source / (key + suffix)).read_bytes()
                if hashlib.sha256(raw).hexdigest() != rows[key][field]:
                    raise ValueError('validated shader payload changed')
                target = output / (key + suffix)
                if target.exists() and target.read_bytes() != raw:
                    raise ValueError('conflicting shader variants')
                target.write_bytes(raw)
            raw = gzip.decompress((source / (key + '.spv.gz')).read_bytes())
            if hashlib.sha256(raw).hexdigest() != rows[key]['spirv_sha256']:
                raise ValueError('decompressed shader hash mismatch')
            entries[key], records[key] = entry, rows[key]
            at = end
        receipts.append(dict(stage=source.name, validation_sha256=digest(source / 'validation.json')))
    full = json.loads((inventory / 'inventory.json').read_text())
    excluded = known_exclusions(full['programs'])
    expected = {p['key'] for p in full['programs']} - {row['key'] for row in excluded}
    if entries.keys() != expected:
        raise ValueError('shader coverage incomplete; no device package will be made')
    index = struct.pack('<3I', 0x494b5644, 2, len(entries)) + b''.join(entries[k] for k in sorted(entries))
    (output / 'shader_index.bin').write_bytes(index)
    write_json(output / 'merge-receipt.json', dict(exported=len(entries), payloads_verified=True,
        index_sha256=hashlib.sha256(index).hexdigest(), sources=receipts,
        inventoried_supported_programs=len(full['programs']), excluded=excluded,
        excluded_stages=full.get('excluded', []), full_game_shader_coverage=not (excluded or full.get('excluded')),
        runtime_rendering_verified=False, records=[records[k] for k in sorted(records)]))


def verify_astc(romfs, work, encoder, preset, game_build="90447"):
    """Generate the receipt from fresh work; no developer's verified.json needed."""
    sys.path.insert(0, str(ROOT / 'dm/tools'))
    import dmx
    dmx.ROOT, dmx._IDX = str(romfs / 'data'), None
    for stream in dmx._FH.values():
        stream.close()
    dmx._FH = {}
    rows = dmx.index()
    names = {}
    for line in (work / 'repl.tsv').read_text().splitlines():
        name = logical_path(line.split('\t', 1)[0])
        if name in names:
            raise ValueError('duplicate ASTC replacement')
        names[name] = work / 'files' / name
    expected = set()
    for name, row in rows.items():
        if not name.endswith('.texture'):
            continue
        original = dmx.read_row(row)
        if len(original) < 36 or original[4] not in (61, 62):
            continue
        logical = logical_path(name.split(':', 1)[-1])
        expected.add(logical)
        converted = names.get(logical)
        if converted is None:
            raise ValueError('missing BC3 conversion: ' + logical)
        data = converted.read_bytes()
        want = 95 if original[4] == 61 else 81
        if len(data) != len(original) or data[:4] != original[:4] or data[4] != want or data[5:36] != original[5:36]:
            raise ValueError('ASTC conversion header/length mismatch: ' + logical)
    expected.add('data/hd/texture_desc_cache.json')
    if names.keys() != expected:
        raise ValueError('ASTC replacement coverage mismatch')
    cache = json.loads(names['data/hd/texture_desc_cache.json'].read_text())
    for entry in cache:
        name = logical_path(entry['filename'])
        if name in expected and name.endswith('.texture') and entry['format'] not in (95, 81):
            raise ValueError('converted texture descriptor still selects BC3')
    write_json(work.parent / 'verified.json', dict(passed=True, game_build=game_build,
        source_manifest_sha256=digest(romfs / 'data/build_manifest.psv'),
        encoder_sha256=digest(encoder), preset=preset, textures_converted=len(expected) - 1,
        files={name: digest(path) for name, path in sorted(names.items())},
        conversion_header_and_coverage_verified=True, gameplay_verified=False))


def executable(value):
    path = Path(shutil.which(value) or value).expanduser().resolve()
    if not path.is_file() or not os.access(path, os.X_OK):
        raise argparse.ArgumentTypeError('executable not found: ' + value)
    return path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    inputs = p.add_mutually_exclusive_group(required=True)
    inputs.add_argument('--package', type=Path, action='append', help='repeat for base NSP and 1.0.28.0 update NSP')
    inputs.add_argument('--extracted', type=Path, help='already merged Update directory containing ExeFS and RomFS')
    p.add_argument('--keys', type=Path)
    p.add_argument('--title-keys', type=Path)
    p.add_argument('--hactool', type=executable)
    p.add_argument('--astcenc', type=executable, required=True)
    p.add_argument('--dotnet', type=executable, required=True, help='.NET 10 SDK')
    p.add_argument('--validator', type=executable, required=True, help='host spirv-val')
    p.add_argument('--mksquashfs', type=executable, required=True)
    p.add_argument('--cc', type=executable, default='cc')
    p.add_argument('--game-build', choices=('90447', '94012'), default='90447')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--resume', action='store_true')
    p.add_argument('--resume-shader-cache', type=Path, help='previous shader_cache.py output; reuse only matching, hash-verified successes')
    p.add_argument('--max-side', type=int, default=128)
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--shader-policy', choices=('strict', 'upstream', 'optimized'), default='strict',
                   help='upstream permits non-required FP contraction; optimized also tries the experimental AST optimizer')
    args = p.parse_args()
    if not 1 <= args.jobs <= 8 or not 16 <= args.max_side <= 4096:
        p.error('jobs 1..8 and max-side 16..4096 required')
    if args.package and (not args.keys or not args.hactool):
        p.error('--package requires --keys and --hactool')
    for module in ('numpy', 'PIL', 'lz4', 'capstone'):
        if importlib.util.find_spec(module) is None:
            p.error('missing Python dependency: ' + module + '; see PREPARE.zh-CN.md')
    if args.extracted:
        args.extracted = args.extracted.expanduser().resolve()
        original = verify_update(args.extracted, args.game_build)
    else:
        original = dict(packages=[dict(size=f.stat().st_size, sha256=digest(f)) for f in args.package])
    sources = {str(f.relative_to(ROOT)): digest(f) for directory in
               ('dm/linux', 'dm/tools', 'dm/gfx/tools', 'dm/gfx/nvn', 'dm/gfx/tex', 'dm/third_party/ryujinx')
               for f in (ROOT / directory).rglob('*') if f.is_file() and
               not any(part in ('out', 'bin', 'obj', '__pycache__') for part in f.relative_to(ROOT).parts)
               and f.suffix in ('.py', '.c', '.h', '.inc', '.tsv', '.cs', '.csproj', '.props', '.targets', '.glsl')}
    identity = dict(schema=1, profile=args.game_build+('-rk3566-drawable-shadow512' if args.game_build=='94012' else '-rk3566-native960x720-shadow512'), original=original,
        maximum_world_edge=args.max_side, shader_policy=args.shader_policy, source_hashes=sources,
        tools={name: digest(getattr(args, name)) for name in
               ('astcenc', 'dotnet', 'validator', 'mksquashfs', 'cc')},
        hactool_sha256=digest(args.hactool) if args.hactool else None,
        input_location=str(args.extracted) if args.extracted else [str(f.resolve()) for f in args.package])
    if args.resume_shader_cache:
        args.resume_shader_cache = args.resume_shader_cache.expanduser().resolve()
        identity['resume_shader_receipt_sha256'] = digest(args.resume_shader_cache / 'validation.json')
    pipeline = Pipeline(args.out.expanduser().resolve(), args.resume, identity)
    py = sys.executable
    if args.extracted:
        update = args.extracted
    else:
        extracted = pipeline.stage('01-extracted', lambda out: extract(
            [f.resolve() for f in args.package], args.keys.resolve(),
            args.title_keys.resolve() if args.title_keys else None, args.hactool, out, game_build=args.game_build))
        update = extracted / 'Update'
    romfs = update / 'RomFS'

    def rewrite(out):
        out.mkdir()
        env = dict(os.environ)
        for name in list(env):
            if name.startswith('DM_'):
                del env[name]
        env.update(DM_TARGET='linux', DM_RULESET='native', DM_PRIVATE_GATES='0',
                   DM_LINUX_OFFLINE='1', DM_LINUX_DISPLAY='drawable' if args.game_build=='94012' else '960x720', DM_LINUX_SHADOW_SIDE='512')
        for name in ('main', 'sdk', 'subsdk0'):
            pipeline.run('rewrite-' + name, [py, ROOT / 'dm/tools/patch_nso.py', update / 'ExeFS' / name, out, name], env)
    modules = pipeline.stage('02-modules', rewrite)

    def build_tools(out):
        out.mkdir()
        pipeline.run('translator-build', [args.dotnet, 'publish', ROOT / 'dm/gfx/tools/dmsh_xlate/dmsh_xlate.csproj',
            '-c', 'Release', '-o', out / 'xlate', '--artifacts-path', out / 'artifacts'])
        pipeline.run('distance-build', [args.cc, '-O2', '-std=c11', ROOT / 'dm/android/tests/t_unused_distance.c', '-o', out / 'distance'])
        pipeline.run('distance-test', [out / 'distance'])
    tools = pipeline.stage('03-tools', build_tools)
    translator = tools / 'xlate/dmsh_xlate'
    env = dict(os.environ, DOTNET_ROOT=str(args.dotnet.parent))
    expected_source = hashlib.sha256(b''.join((ROOT / 'dm/gfx/tools/dmsh_xlate' / f).read_bytes()
        for f in ('Program.cs', 'dmsh_xlate.csproj'))).hexdigest()[:16]
    actual = subprocess.check_output([str(translator), '--source-sha'], env=env, text=True).strip()
    if actual != expected_source:
        raise ValueError('translator embedded source differs from checkout')
    inventory = pipeline.stage('04-inventory', lambda out: pipeline.run('shader-inventory', [py,
        LINUX / 'shader_inventory.py', '--romfs', romfs, '--main', update / 'ExeFS/main', '--out', out,
        '--shader-policy', args.shader_policy]))
    spv = pipeline.stage('05-translated', lambda out: pipeline.run('shader-translate', [translator, inventory / 'jobs.json', out], env))

    def validate(name, inv, translated, resume=None):
        return pipeline.stage(name, lambda out: pipeline.run(name, [py, LINUX / 'shader_cache.py',
            '--inventory', inv, '--spv', translated, '--validator', args.validator,
            '--distance-tool', tools / 'distance', '--jobs', args.jobs, '--out', out] +
            (['--resume', resume] if resume else [])))
    validated = validate('06-validated', inventory, spv, args.resume_shader_cache)
    variants = pipeline.stage('07-buffer-inventory', lambda out: buffer_inventory(inventory, spv, validated, out))
    variant_spv = pipeline.stage('08-buffer-translated', lambda out: pipeline.run('buffer-translate', [translator, variants / 'jobs.json', out], env))
    variant_cache = validate('09-buffer-validated', variants, variant_spv)
    cache = pipeline.stage('10-shader-cache', lambda out: merge_caches([validated, variant_cache], inventory, out))

    def astc(out):
        out.mkdir()
        pipeline.run('astc-convert', [py, ROOT / 'dm/tools/astc_pack.py', romfs, out / 'work',
                                    '--astcenc', args.astcenc, '--preset', 'thorough'])
        verify_astc(romfs, out / 'work', args.astcenc, 'thorough', args.game_build)
    textures = pipeline.stage('11-astc', astc)
    pack = pipeline.stage('12-handheld', lambda out: pipeline.run('handheld-pack', [py,
        LINUX / 'prepare_handheld.py', '--game-build', args.game_build, '--romfs', romfs, '--astc-work', textures / 'work',
        '--shader-cache', cache, '--out', out, '--max-side', args.max_side, '--jobs', args.jobs,
        '--cc', args.cc, '--mksquashfs', args.mksquashfs]))

    def assemble(out):
        game = out / 'game'
        game.mkdir(parents=True)
        shutil.copytree(romfs, game / 'Update/RomFS', copy_function=shutil.copy2)
        shutil.copytree(modules, game / 'linux-native')
        shutil.copytree(pack / 'device', game / 'handheld-prepared-128')
        write_json(game / 'game-manifest.json', dict(game_build=args.game_build, files=tree_files(game)))
        for name in ('install_game.py','classic_bundle.py','device_profiles.py'):
            shutil.copyfile(LINUX / name, out / name)
        from device_profiles import DEVICE_IDS
        for device_id in DEVICE_IDS:
            target=out/'devices'/device_id;target.mkdir(parents=True)
            shutil.copyfile(LINUX/'devices'/device_id/'profile.json',target/'profile.json')
    device = pipeline.stage('13-device', assemble)
    write_json(pipeline.root / 'complete.json', dict(complete=True, game_build=args.game_build,
        device_directory='13-device', game_files_private=True, keys_in_device_package=False,
        computer_preparation=True, gpu_driver_cache_precompiled=False, gameplay_verified=False))
    print('Ready: copy only ' + str(device) + '; this private output contains YOUR game, do not redistribute.', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit('prepare: ' + str(error))
