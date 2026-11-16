#!/usr/bin/env python3
# INPUT: Explicit open-source preparation modules, translator sources and licenses.
# OUTPUT: Public source-only preparation files; no local output or tool caches traversed.
# POS: Fixed distribution allowlist used by package_runtime.py --with-preparer.
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LINUX_FILES = (
    'prepare_classic.py','classic_bundle.py','device_profiles.py','compact_classic.py','prepare_game.py','extract_local.py',
    'prepare_handheld.py','handheld_mips.py','prepare_astc_blocks.py','prepare_astc_sprites.py',
    'install_handheld_pack.py','install_game.py',
    'prepare_classic_native.py','terrain_resolution.py','native_hd_patch.c','native_hd_policy.h','native_hd_94012.h',
    'classic_resource_gate.py','classic_resource_gate.S.in','classic_resource_94012.py',
    'classic_frontend_gate.py','classic_frontend_gate.S.in',
    'recompile_shaders.py','optimize_shaders.py','prepare_model_lighting.py',
    'prepare_classic_display.py','spirv_classic_display.py',
    'prepare_shader_nan_minmax.py','spirv_nan_minmax.py',
    'spirv_model_lighting.py','spirv_c1_constants.py','spirv_support_constants.py','spirv_precision.py',
    'prepare_classic_frontend.py','prepare_classic_weather.py','prepare_remaster_quality.py',
    'prepare_classic_hd_assets.py','prepare_classic_hd_suppression.py',
    'shader_inventory.py','shader_cache.py','requirements-prepare.txt')
EXTRA_FILES = (
    'dm/tools/patch_nso.py','dm/tools/astc_pack.py','dm/tools/casc_overlay.py','dm/tools/dmx.py',
    'dm/tools/relative_jump.py','dm/tools/community_patch_plan.py',
    'dm/tools/nso_inventory.py','dm/tools/bnsh_vulkan_cache.py','dm/tools/spirv_static_offsets.py',
    'dm/gfx/tools/dmsh_facts.py','dm/gfx/nvn/shader_facts.tsv','dm/gfx/tools/dmsh_xlate/Program.cs',
    'dm/gfx/tools/dmsh_xlate/dmsh_xlate.csproj','dm/ios/tools/touch_layout.py',
    'dm/android/tests/t_unused_distance.c','dm/gfx/nvn/nvn_vk_unused_distance.h',
    'dm/gfx/nvn/nvn_tex.c','dm/gfx/nvn/nvn_tex.h',
    'dm/gfx/tex/nvn_deswizzle.c','dm/gfx/tex/nvn_deswizzle.h','LICENSE')


def preparer_files():
    paths = [*(ROOT/'dm/linux'/name for name in LINUX_FILES), *(ROOT/name for name in EXTRA_FILES)]
    ryu = ROOT/'dm/third_party/ryujinx'
    for path in ryu.rglob('*'):
        relative = path.relative_to(ryu)
        if any(n in ('bin','obj','.git') for n in relative.parts):
            continue
        if path.is_file() and (path.suffix in ('.cs','.csproj','.props','.targets','.glsl','.md','.txt') or path.name=='LICENSE'):
            paths.append(path)
    files = {}
    from device_profiles import DEVICE_IDS
    paths += [ROOT/'dm/linux/devices'/device_id/'profile.json' for device_id in DEVICE_IDS]
    for path in paths:
        if path.is_symlink() or not path.is_file():
            raise ValueError('missing/nonregular public preparer source: '+str(path.relative_to(ROOT)))
        files['preparer/'+path.relative_to(ROOT).as_posix()] = path
    files['prepare-classic.sh'] = ROOT/'dm/linux/prepare-classic.sh'
    return files
