#!/usr/bin/env python3
# INPUT: A checked-in model ID and its reviewed classic menu/memory preset.
# OUTPUT: Reproducible preparation options and model-specific install paths.
# POS: Shared desktop/install routing; a target is never a gameplay certificate.
import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
DEVICE_IDS = ('miniloong-pocket-one-rk3566-4gb', 'rk3566-2gb')
CHARACTER_PRESET = dict(name='rk3566-classic-character-only-v1', buffer_pool_mib=256,
    texture_pool_mib=320, cpu_pool_mib=250, terrain_auto=True,
    classic_frontend_resources=True, classic_resource_gate=False,
    keep_character=True, selected_character_only=True,
    menu_background=False, classic_sharpness=0.4, pool_windows=True,
    compact_array_images=True, shared_pool_min_mib=192, shared_pool_max_mib=256)
FLAT_PRESET = dict(CHARACTER_PRESET,name='rk3566-classic-flat-v1',
    classic_frontend_resources=False,classic_resource_gate=True,
    keep_character=False,selected_character_only=False)
DEVICE_PRESETS = dict(zip(DEVICE_IDS,(CHARACTER_PRESET,FLAT_PRESET)))


def load_device(device_id):
    if device_id not in DEVICE_IDS or not re.fullmatch('[a-z0-9-]+',device_id):
        raise ValueError('unknown device profile: '+str(device_id))
    path=HERE/'devices'/device_id/'profile.json'
    if path.is_symlink() or not path.is_file():
        raise ValueError('missing regular device profile: '+device_id)
    value=json.loads(path.read_text())
    if (set(value)!= {'schema','id','model','soc','architecture','memory_mib','preset'} or
            value['schema']!=1 or value['id']!=device_id or value['soc']!='rk3566' or
            value['architecture']!='aarch64' or value['memory_mib']!={'miniloong-pocket-one-rk3566-4gb':4096,'rk3566-2gb':2048}[device_id] or
            value['preset']!=DEVICE_PRESETS[device_id]['name'] or not isinstance(value['model'],str)):
        raise ValueError('unreviewed device profile: '+device_id)
    resolved=dict(value,settings=DEVICE_PRESETS[device_id].copy())
    encoded=json.dumps(resolved,sort_keys=True,separators=(',',':')).encode()
    return dict(resolved,profile_sha256=hashlib.sha256(encoded).hexdigest())


def install_paths(profile):
    device_id=profile['id']
    if load_device(device_id)!=profile:
        raise ValueError('device profile identity mismatch')
    return dict(runtime_root=Path('/userdata/d2r-linux')/device_id/'runtime',
        profile_root=Path('/userdata/d2r-linux')/device_id/'profile',
        controllers=Path('/mnt/sdcard/roms/ports/PortMaster/gamecontrollerdb.txt'),
        entry=Path('/mnt/sdcard/roms/ports')/('Je-暗黑破坏神II_NS（'+('2G' if device_id=='rk3566-2gb' else '4G')+'版）.sh'),
        game_root=Path('/mnt/sdcard/ports/d2r-linux')/device_id/'game')


def launcher_options(profile):
    if load_device(profile['id'])!=profile:
        raise ValueError('device profile identity mismatch')
    settings=profile['settings']
    return dict(D2R_POOL_WINDOWS=int(settings['pool_windows']),
        DM_LINUX_COMPACT_ARRAY_IMAGES=int(settings['compact_array_images']),
        D2R_SHARED_POOL_MIN_MB=settings['shared_pool_min_mib'],
        D2R_SHARED_POOL_MAX_MB=settings['shared_pool_max_mib'])


def verify_components(profile,native,frontend):
    if load_device(profile['id'])!=profile:
        raise ValueError('device profile identity mismatch')
    game_build=native.get('game_build','90447')
    if game_build not in ('90447','94012') or frontend.get('game_build','90447')!=game_build or game_build=='94012' and profile['id']!='rk3566-2gb':
        raise ValueError('component game build mismatch')
    settings=profile['settings'];pools=native.get('graphics_pool_policy',{})
    if (pools.get('buffer_pool_mib')!=settings['buffer_pool_mib'] or
            pools.get('texture_pool_mib')!=settings['texture_pool_mib'] or
            native.get('cpu_pool_policy',{}).get('cpu_pool_mib')!=settings['cpu_pool_mib'] or
            (game_build=='90447' and (native.get('terrain_quality_policy') or {}).get('automatic') is not True) or
            native.get('resource_load_policy',{}).get('policy')!=('classic-resource-gate-94012-v1' if game_build=='94012' else 'classic-frontend-resource-gate-90447-v1' if settings['keep_character'] else 'classic-resource-gate-90447-v3') or
            (game_build=='94012' and frontend.get('handheld_character_sheet') is not True) or
            frontend.get('policy')!=('classic-frontend-character-v5' if settings['keep_character'] else 'classic-frontend-no-preview-v3')):
        raise ValueError('prepared components differ from the selected device preset')
