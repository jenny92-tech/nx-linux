#!/usr/bin/env python3
# INPUT: User's verified 90447/94012 Linux modules and an existing host C compiler.
# OUTPUT: Private HD/resource controls, optional terrain/pool/batch sizes, and hashes.
# POS: Computer-only optional quality experiment; no original writes or deployment.
"""Prepare classic-only native HD submission controls from your own prepared files."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
from terrain_resolution import prepare_auto
from classic_resource_gate import prepare_resource_gate

HERE = Path(__file__).resolve().parent
SUFFIXES = ('patched.bin', 'fixups.tsv', 'gates.tsv', 'params.tsv', 'sites.tsv', 'cuts.tsv', 'obs.tsv')

# Reviewed 90447 graphics-device configuration, before the original constructor.
# Packed upper flag/alignment fields and every surrounding instruction survive.
POOL_CONFIG_OFFSET = 0x722BA8
POOL_CONFIG_WORDS = (
    0xD2A58C09, 0xF2E00409, 0xB90233EA, 0xD2A0004A,
    0xF2C0280A, 0xA919FFFF, 0xF80F4348, 0xD2A20808,
    0xF2E01408, 0xF9011FE8, 0xD2A32008, 0xF2E00208,
    0xF90127E9, 0xD2880009, 0xF2C01009, 0xF90123E8,
)

# Reviewed 90447 CPU-side TLSF pool configuration, passed to main+0xc9802c.
# Change the size in the original configuration, so allocation and pool setup agree.
CPU_POOL_CONFIG_OFFSET = 0x134834
CPU_POOL_CONFIG_WORDS = (
    0x52880008, 0x52A1F40A, 0x5280C009, 0xA9012BE8, 0x52810008, 0xA90223E9,
)

# Reviewed 90447 shared render-construction object pool. These are slots per
# allocation chunk, not a hard live-object limit; the original growth path stays.
RENDER_OBJECT_POOL_CONFIG_OFFSET = 0x10ECC10
RENDER_OBJECT_POOL_CONFIG_WORDS = (
    0x9000F933, 0x91338273, 0xF900467F, 0x52800108,
    0xB9001268, 0xF9417A88, 0x91006269, 0xA901A669,
    0xD2836309, 0xF2C40009, 0x91026260, 0xA903FE7F,
    0x2A1F03E1, 0x2A1F03E2, 0xA9002668, 0x9100A268,
    0xA902A268, 0x52800028, 0x2912227F, 0xA904FE7F,
    0xA905FE7F, 0xA906FE7F, 0xA907FE7F,
)
RENDER_OBJECT_POOL_GROWTH_OFFSET = 0x19DB244
RENDER_OBJECT_POOL_GROWTH_WORDS = (
    0x29412668, 0x1B087D36, 0xF9400260, 0xF9400008,
    0xB9401262, 0x1100C2C1, 0xF9400908, 0xD63F0100,
)


# 90447 Terrain 0/1 are generated HD terrain atlases, independent of the
# classic world resolution. Keep all 18x18 cells; halve each cell's texel side
# together with the atlas, compute dispatch extent, recreation check and query.
# The original consumers still derive viewport/uniform sizes from these globals.
TERRAIN_64_PATCHES = (
    (0x153A920, 0x52812009, 0x52809009),  # initial atlas side: 2304 -> 1152
    (0x153A92C, 0x52801009, 0x52800809),  # initial tile side: 128 -> 64
    (0x153A938, 0x52800209, 0x52800109),  # initial dispatch: 16 -> 8
    (0x1541168, 0x7124011F, 0x7112011F),  # recreation compares the same atlas side
    (0x15411A4, 0x52812008, 0x52809008),  # recreated atlas side
    (0x15411B0, 0x52801009, 0x52800809),  # recreated tile side
    (0x15411BC, 0x52800209, 0x52800109),  # recreated dispatch
    (0x15431FC, 0xD2812000, 0xD2809000),  # queried width
    (0x1543200, 0xF2C12000, 0xF2C09000),  # queried height, upper 32 bits
)
TERRAIN_CONTEXT = (
    (0x153A10C, '68d700f013f148b9'),  # atlas constructor reads shared size
    (0x153A374, '68d700f008f548b9'),  # dispatch reads shared extent
    (0x153C320, '68d700b001f148b9'),  # first viewport
    (0x153C9D4, '68d700b001f148b9'),  # second viewport
    (0x1540420, '48d700b008f948b9'),  # per-cell parameters read shared size
    (0x1540448, '9f4b00f157028052'),  # retain 18 cells per row
    (0x1540464, '1f4900f1'),          # retain 18 rows
    (0x14BEA60, '48db00f008f148b9'),  # terrain shader uniforms
    (0x1BB0FE9, '5465727261696e203000'),
    (0x1B89921, '5465727261696e203100'),
    (0x1BA560C, '7465727261696e5f636f6d706f736974655f6300'),
)


def prepare_terrain_resolution(image, tile_side=None, automatic=False):
    """Opt-in classic experiment; resize generated terrain resources coherently."""
    if automatic and tile_side is not None:
        raise ValueError('choose fixed or automatic terrain resolution')
    if tile_side is None and not automatic:
        return None
    if not automatic and (type(tile_side) is not int or tile_side != 64):
        raise ValueError('only the reviewed 90447 terrain tile side 64 is supported')
    checks = [(at, bytes.fromhex(raw)) for at, raw in TERRAIN_CONTEXT]
    checks += [(at, struct.pack('<I', before)) for at, before, _ in TERRAIN_64_PATCHES]
    for at, expected in checks:
        if image[at:at+len(expected)] != expected:
            raise ValueError('unreviewed or already changed 90447 terrain configuration at 0x%x' % at)
    if automatic:
        return prepare_auto(image)
    for at, _, after in TERRAIN_64_PATCHES:
        struct.pack_into('<I', image, at, after)
    return dict(policy='classic-terrain-atlas-resolution-v1', original_atlas_side=2304,
                atlas_side=1152, original_tile_side=128, tile_side=64, tile_grid=[18, 18],
                original_dispatch_extent=16, dispatch_extent=8,
                patches=[dict(offset=at, original_word=before, output_word=after)
                         for at, before, after in TERRAIN_64_PATCHES],
                graphics_quality_changed=True, classic_world_resolution_changed=False,
                pool_capacity_changed=False, runtime_rendering_verified=False,
                physical_memory_saving_verified=False)


def prepare_render_object_batch(image, units_per_chunk=None):
    """Opt-in smaller chunks; retain object size, allocation and growth behavior."""
    if units_per_chunk is None:
        return None
    if type(units_per_chunk) is not int or units_per_chunk != 64:
        raise ValueError('only the reviewed 90447 render-object batch size 64 is supported')
    checks = ((RENDER_OBJECT_POOL_CONFIG_OFFSET, RENDER_OBJECT_POOL_CONFIG_WORDS),
              (RENDER_OBJECT_POOL_GROWTH_OFFSET, RENDER_OBJECT_POOL_GROWTH_WORDS))
    for offset, words in checks:
        expected = struct.pack('<' + str(len(words)) + 'I', *words)
        if image[offset:offset + len(expected)] != expected:
            raise ValueError('unreviewed or already changed 90447 render-object pool at 0x%x' % offset)
    offset = RENDER_OBJECT_POOL_CONFIG_OFFSET + 9 * 4
    output_word = 0xF2C00009 | (units_per_chunk << 5)
    struct.pack_into('<I', image, offset, output_word)
    return dict(policy='classic-render-object-batch-v1', original_units_per_chunk=8192,
                units_per_chunk=units_per_chunk, unit_bytes=6936,
                instruction_offset=offset, original_word=0xF2C40009, output_word=output_word,
                controller_offset=0x3010CE0, hard_object_limit_added=False,
                growth_code_changed=False, graphics_quality_changed=False,
                runtime_rendering_verified=False, physical_memory_saving_verified=False)


def prepare_cpu_pool_size(image, cpu_pool_mib=None, game_build="90447"):
    """Opt-in capacity experiment; keep the original TLSF allocator and all game data."""
    if cpu_pool_mib is None:
        return None
    if type(cpu_pool_mib) is not int or not 64 <= cpu_pool_mib <= 250:
        raise ValueError('90447 CPU pool size must be integer MiB, 64..250')
    expected = struct.pack('<6I', *CPU_POOL_CONFIG_WORDS)
    start = 0x1348c8 if game_build == "94012" else CPU_POOL_CONFIG_OFFSET
    if image[start:start+len(expected)] != expected:
        raise ValueError('unreviewed or already changed 90447 CPU pool configuration')
    words = list(CPU_POOL_CONFIG_WORDS)
    words[1] = 0x52A0000A | ((cpu_pool_mib << 4) << 5)
    image[start:start+len(expected)] = struct.pack('<6I', *words)
    return dict(policy='classic-cpu-pool-capacity-v1', original_cpu_pool_mib=250,
                cpu_pool_mib=cpu_pool_mib, config_offset=start,
                original_words=list(CPU_POOL_CONFIG_WORDS), output_words=words,
                graphics_quality_changed=False, runtime_rendering_verified=False,
                physical_memory_saving_verified=False)


def prepare_pool_sizes(image, buffer_mib=None, texture_mib=None, game_build="90447"):
    """Resize original allocator capacities; never lie about an existing pool's size."""
    if buffer_mib is None and texture_mib is None:
        return None
    buffer_mib = 400 if buffer_mib is None else buffer_mib
    texture_mib = 710 if texture_mib is None else texture_mib
    for value, maximum in ((buffer_mib, 400), (texture_mib, 710)):
        if type(value) is not int or not 64 <= value <= maximum:
            raise ValueError('90447 graphics pool sizes must be integer MiB, 64..400 / 64..710')
    expected = struct.pack('<16I', *POOL_CONFIG_WORDS)
    start = 0x8524a8 if game_build == "94012" else POOL_CONFIG_OFFSET
    if image[start:start+len(expected)] != expected:
        raise ValueError('unreviewed or already changed 90447 graphics pool configuration')
    words = list(POOL_CONFIG_WORDS)
    words[0] = 0xD2A00009 | ((texture_mib << 4) << 5)
    words[10] = 0xD2A00008 | ((buffer_mib << 4) << 5)
    image[start:start+len(expected)] = struct.pack('<16I', *words)
    return dict(policy='classic-graphics-pool-capacity-v1',
                original_buffer_pool_mib=400, original_texture_pool_mib=710,
                buffer_pool_mib=buffer_mib, texture_pool_mib=texture_mib,
                config_offset=start, original_words=list(POOL_CONFIG_WORDS), output_words=words,
                graphics_quality_changed=False, runtime_rendering_verified=False)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(modules, out, compiler, buffer_pool_mib=None, texture_pool_mib=None, cpu_pool_mib=None,
            terrain_tile_side=None, terrain_auto=False, classic_resource_gate=False, render_object_batch=None,
            frontend_resources=False, game_build="90447"):
    if game_build not in ('90447', '94012'):
        raise ValueError('unsupported native classic build')
    if game_build == '94012' and (terrain_auto or terrain_tile_side is not None or render_object_batch is not None):
        raise ValueError('94012 currently supports original terrain and render-object batches only')
    if classic_resource_gate and frontend_resources:
        raise ValueError('choose either flat-menu or retained-frontend resource policy')
    modules, out = modules.resolve(), out.resolve()
    if out.exists() or modules in out.parents or out in modules.parents:
        raise ValueError('choose a nonexistent, separate module output directory')
    policy = (HERE / ('native_hd_94012.h' if game_build == '94012' else 'native_hd_policy.h')).read_text()
    build = re.search(r'#define DM_NATIVE_HD_BUILD "([0-9a-f]{32})"', policy)[1]
    definitions = dict(re.findall(r'^#define (DM_NATIVE_HD_\w+) (0x[0-9a-f]+)u$', policy, re.M))
    required = dict(BUILD_ID=build, LINUX_TLS_OFFSET_IDX='8',
                    KEY_PAGE_OFF=definitions['DM_NATIVE_HD_KEYS'], PATCH_OFF=definitions['DM_NATIVE_HD_PATCH'],
                    TEXT_END=definitions['DM_NATIVE_HD_TEXT_END'], RO_OFF=definitions['DM_NATIVE_HD_RO'],
                    DATA_OFF=definitions['DM_NATIVE_HD_DATA'])
    header = dict(re.findall(r'^# (\w+)\s+(\S+)\s*$', (modules / 'main.fixups.tsv').read_text(), re.M))
    for name, expected in required.items():
        actual = header.get(name)
        if (actual != expected if name == 'BUILD_ID' else actual is None or int(actual, 0) != int(expected, 0)):
            raise ValueError('unreviewed prepared main layout: ' + name)
    files = {}
    for name in ('main', 'sdk', 'subsdk0'):
        for suffix in SUFFIXES:
            filename = name + '.' + suffix
            path = modules / filename
            if path.is_symlink() or not path.is_file():
                raise ValueError('missing/nonregular prepared module file: ' + filename)
            files[filename] = dict(bytes=path.stat().st_size, sha256=digest(path))
    compiler_path = shutil.which(str(compiler))
    if not compiler_path:
        raise ValueError('existing host C compiler required')
    compiler_version = subprocess.check_output([compiler_path, '--version'], text=True).splitlines()[0]
    out.mkdir(parents=True, mode=0o700)
    for name in files:
        shutil.copyfile(modules / name, out / name)
    graphics_policy = None
    if game_build == '94012':
        image = bytearray((out / 'main.patched.bin').read_bytes())
        at = 0xb15de8
        if image[at:at+4] != bytes.fromhex('28020054'):
            raise ValueError('unreviewed 94012 classic-era graphics check')
        image[at:at+4] = bytes.fromhex('1f2003d5')
        (out / 'main.patched.bin').write_bytes(image)
        with (out / 'main.cuts.tsv').open('a') as stream:
            stream.write('0xb15de8\t28020054\t1f2003d5\tLinux classic\tclassic graphics for owned RotW era\n')
        graphics_policy = dict(policy='rotw-classic-display-v1', offset=at,
            original_word=0x54000228, output_word=0xd503201f,
            legacy_placeholder_appearance=True, game_era_unchanged=True,
            gameplay_logic_changed=False, runtime_rendering_verified=False)
    with tempfile.TemporaryDirectory(prefix='d2r-native-hd-tool-') as temporary:
        tool = Path(temporary) / 'patch'
        subprocess.run([compiler_path, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-Wno-misleading-indentation', *(['-DDM_LINUX_GAME_94012'] if game_build == '94012' else []), str(HERE / 'native_hd_patch.c'), '-o', str(tool)], check=True)
        pending = out / 'main.native-next.bin'
        result = subprocess.check_output([str(tool), str(out / 'main.patched.bin'), str(pending)], text=True)
        report = json.loads(result)
        tool_hash = digest(tool)
        if classic_resource_gate or frontend_resources or terrain_auto or any(value is not None for value in (buffer_pool_mib, texture_pool_mib, cpu_pool_mib, terrain_tile_side, render_object_batch)):
            image = bytearray(pending.read_bytes())
            if buffer_pool_mib is not None or texture_pool_mib is not None:
                report['graphics_pool_policy'] = prepare_pool_sizes(image, buffer_pool_mib, texture_pool_mib, game_build)
            if cpu_pool_mib is not None:
                report['cpu_pool_policy'] = prepare_cpu_pool_size(image, cpu_pool_mib, game_build)
            if render_object_batch is not None:
                report['render_object_pool_policy'] = prepare_render_object_batch(image, render_object_batch)
            if terrain_tile_side is not None or terrain_auto:
                report['terrain_quality_policy'] = prepare_terrain_resolution(image, terrain_tile_side, terrain_auto)
            if classic_resource_gate or frontend_resources:
                report['resource_load_policy'] = prepare_resource_gate(image, frontend=frontend_resources, game_build=game_build)
            pending.write_bytes(image)
        pending.replace(out / 'main.patched.bin')
    report.update(complete=True, build_id=build, game_build=game_build, classic_graphics_policy=graphics_policy, compiler=compiler_version, tool_sha256=tool_hash,
                  source_files=files, source_sha256={p.name: digest(p) for p in
                    (Path(__file__), HERE / 'native_hd_patch.c', HERE / 'native_hd_policy.h', HERE / 'terrain_resolution.py',
                     HERE / 'classic_resource_gate.py', HERE / 'classic_resource_gate.S.in',
                     HERE / 'classic_frontend_gate.py', HERE / 'classic_frontend_gate.S.in',
                     HERE / 'native_hd_94012.h', HERE / 'classic_resource_94012.py')},
                  output_files={name: dict(bytes=(out/name).stat().st_size, sha256=digest(out/name)) for name in files},
                  input_unchanged=all(digest(modules/n) == r['sha256'] for n, r in files.items()),
                  requires_runtime_native_hd_validator=True, quality_policy='classic-only native HD submission experiment',
                  runtime_rendering_verified=False, target_fps_verified=False, game_data_or_keys_uploaded=False)
    if not report['input_unchanged']:
        raise ValueError('source modules changed during preparation')
    (out / 'classic-native.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game-build', choices=('90447', '94012'), default='90447')
    p.add_argument('--modules', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--cc', default='cc', help='existing HOST compiler; not an ARM64 guest executable')
    p.add_argument('--buffer-pool-mib', type=int, help='optional original GPU-buffer capacity, 64..400 MiB; requires device testing')
    p.add_argument('--texture-pool-mib', type=int, help='optional original texture-storage capacity, 64..710 MiB; requires device testing')
    p.add_argument('--cpu-pool-mib', type=int, help='experimental original 250 MiB CPU pool capacity, 64..250 MiB; only 128 MiB has short camp/inventory testing')
    p.add_argument('--render-object-batch', type=int, choices=(64,), help='experimental 90447 render-object allocation batch: 64 slots with original growth; requires device testing')
    terrain = p.add_mutually_exclusive_group()
    terrain.add_argument('--terrain-tile-side', type=int, choices=(64,), help='experimental 90447 terrain atlases: fixed 1152-square / 64-pixel cells')
    terrain.add_argument('--terrain-auto', action='store_true', help='experimental terrain storage follows actual internal height through the original rebuild path; startup display must be 960x720')
    resources = p.add_mutually_exclusive_group()
    resources.add_argument('--classic-resource-gate', action='store_true',
                   help='90447 classic-only experiment: reject HD world reads before allocation/decode and omit 3D menu widgets; keeps shared UI/resources')
    resources.add_argument('--classic-frontend-resources', action='store_true',
                   help='90447/94012 experiment: retain original 3D frontend roots and cleanup; restore the classic resource gate when the last root closes; requires the complete frontend asset chain')
    a = p.parse_args()
    result = prepare(a.modules, a.out, a.cc, a.buffer_pool_mib, a.texture_pool_mib, a.cpu_pool_mib,
                     a.terrain_tile_side, a.terrain_auto, a.classic_resource_gate, a.render_object_batch,
                     a.classic_frontend_resources, a.game_build)
    print(json.dumps(dict(complete=True, build_id=result['build_id'], sites=result['sites'],
                         runtime_rendering_verified=False, output=str(a.out))))


if __name__ == '__main__':
    main()
