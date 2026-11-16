#!/usr/bin/env python3
# INPUT: User-owned supported RomFS, verified handheld pack and a new output.
# OUTPUT: Verified menu overlay, optionally retaining the selected character.
# POS: Offline classic-preset preparation; no source/save edits or game execution.
"""Prepare a lighter frontend while retaining the menu controls and labels.

--keep-character retains the original cameras and selected-player display.
--keep-background also retains scenery, terrain, fog and authored lights;
dynamic FX/cloth are removed; --keep-menu-shadows retains outdoor shadows.
The matching Linux runtime
can cache that static scene while keeping UI/composition live. --astc-work
restores verified original player textures. --restore-menu-textures also
retains the original dedicated menu scenery/sky mip chains. Legacy black-background modes
remain available for measurements.
Actual preview visibility, performance and entry/exit require device checks.
--menu-camera-fov optionally enlarges the retained scene through the original
camera projection; it does not upscale the finished image or change UI bounds.
--light-creation omits 94012 creation scenery and its preload dependencies,
retaining all eight class widgets and their authored lighting.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import sys
import zlib

from install_handheld_pack import install, sha256
from prepare_classic_weather import SOURCE_HASHES, disable_rain
from prepare_remaster_quality import read_asset, strict_json, cut_biome, cut_visual
from handheld_mips import NativeLayout, FORMATS, logical_path, read_texture
import casc_overlay as casc

LAYOUT = 'data/global/ui/layouts/mainmenupanelhd.json'
LAYOUT_SHA256 = '1d76dc8a77a3c80ce0592ff90331430f68a0fc878032ee2d22bfdaba781916ea'
POLICY = 'classic-frontend-no-preview-v1'
CHARACTER_POLICY = 'classic-frontend-character-v2'
PRELOAD_LAYOUT_SOURCES = {
    'data/global/ui/layouts/controller/console/mainmenupanelhd.json': '72c658d63c4479a86d2879fb69d77e6ce87255fef850c912ea8a99e709a0a6df',
    'data/global/ui/layouts/controller/mainmenupanelhd.json': 'edef10b146cec59cb700288076f728422dedcca4eb958dc1aa9b4165c9ff6977',
    'data/global/ui/layouts/controller/pc/handheld/mainmenupanelhd.json': 'e3f192768b2a9211ebfb1dab40ce42df929a8c3a6b4d46fa7fdc511ec4c65144',
}
PRELOAD_LAYOUT_SOURCES_94012 = {
    'data/global/ui/layouts/controller/console/mainmenupanelhd.json': '6166505cf89f69525dda3583b86145cee705957aee3b544ef99dbb3154ceb6f4',
    'data/global/ui/layouts/controller/mainmenupanelhd.json': '61cc95da6ee630ebb89ff5d37355b43f6072fca4347446501447ad30c5169ad1',
    'data/global/ui/layouts/controller/pc/handheld/mainmenupanelhd.json': '83237acb57eb2c7bbd1a80660fe980f65777f53be82f9ac2f4faedf39b4be99d',
}
SCENE = 'data/hd/env/preset/ui/frontendact2.json'
BIOME = 'data/hd/env/biome/act2_frontend_biome.json'
CHARACTER_SOURCES = {
    SCENE: '2c6c925766052789382fb97385fa9d7e72534a6fa3c9acb86cf0ed5b2352479d',
    BIOME: '489985ce6a55bec6dcb0f808d76a974609aa2f8b1b1e012cde9b282e6054da3f',
    'data/hd/env/vis/ui_frontend_act1_vignette.json': '30df0c61a094f4ede95e245a3ebe9784ced0906c52419022d9b78c1bd0095d02',
    'data/hd/env/vis/ui_frontend_act2_vignette.json': 'cc6bdf09e5f680e3d036b45de5ec57b5f36198989aab94fc3bd113b43bf59dc2',
    'data/hd/env/vis/ui_frontend_act3_vignette.json': '8046f8217e3ba041f0bac6e771f2e004a18dcdf4587b31d166fe94566a7880ad',
    'data/hd/env/vis/ui_frontend_act4_vignette.json': 'f936d9c6d3bdc06b80e6c7b1ef10f5d17b0d4cc419eb716f4fa67129990837cb',
    'data/hd/env/vis/ui_frontend_act5_vignette.json': '5291d3315a14d22fca951e802f5fb6afc192371b2637ccade10fcf5d99ac67ee',
}
CHARACTER_SOURCES_94012 = dict(CHARACTER_SOURCES, **{
    SCENE: '9b83dffb74a0ef4eb9bf143554a08476d291c1a579c7aca71c7f9504421f8e6d',
})
CACHE = 'data/hd/texture_desc_cache.json'
CHARACTER_OPTIONS = 'data/global/ui/layouts/controller/console/characteroptionspanelhd.json'
CHARACTER_OPTIONS_SHA256 = '0b160d627c70362ab97106f03f8eb2d7e54e56da3f69b2acf65f2c5775da6cf0'
CHARACTER_STATS = 'data/global/ui/layouts/controller/characterstatspanelhd.json'
CHARACTER_STATS_94012_SHA256 = 'fd7d7c8da1933a7a99159e3ce3eed398c31abdb4491e1c26c31c84c760a6b92b'
CREATION_LAYOUT = 'data/global/ui/layouts/charactercreatepanelhd.json'
CREATION_SCENE = 'data/hd/env/preset/ui/charactercreate.json'
CREATION_BIOME = 'data/hd/env/biome/ui_characterselectscreen.json'
CREATION_SOURCES_94012 = {
    CREATION_LAYOUT: '59e65ee3bbdef06d004b5e59667eae44c798e46c80fd63e7566328204975602a',
    CREATION_SCENE: '2783b1d241441ed983fd9f145291d71553923f92734c3480b619c46440a6bb12',
    CREATION_BIOME: 'a5ad9353975e94c070b7f821feacce26dfa81e39bf02d63252fc64d9eab54a3c',
}


def fit_character_stats(raw):
    """Keep the whole controller sheet within the 2880-wide 4:3 UI canvas.

    The console profile expands its 2740-wide sheet by 1.16, clipping the
    resistance column at 4:3. Override this sheet's rectangle only; the 1.04
    scale preserves its center and fits 4:3 and wider handheld displays.
    """
    text = raw.decode('utf-8')
    pattern = r'("rect"\s*:\s*)"\$CharacterStatsPanelRect"'
    rect = '{ "x": -1425, "y": -531, "width": 2740, "height": 1416, "scale": 1.04 }'
    text, count = re.subn(pattern, lambda m: m[1]+rect, text)
    if count != 1:
        raise ValueError('one original controller character-sheet rectangle required')
    return text.encode('utf-8')


def fit_character_options(raw):
    """Fit six mode cards inside the native 2880x2160 logical 4:3 canvas.

    Only rectangle numbers change. Text, styles, controls, navigation, flags
    and the center selection buttons retain their original bytes.
    """
    text = raw.decode('utf-8')
    seen = set()
    side = (r'("rect"\s*:\s*\{\s*"x"\s*:\s*)(-1601|173)'
            r'(\s*,\s*"y"\s*:\s*)(493|899|1306)'
            r'(\s*,\s*"width"\s*:\s*)1471(\s*,\s*"height"\s*:\s*)360(\s*\})')
    def panel(m):
        pair = (int(m[2]), int(m[4]))
        if pair in seen:
            raise ValueError('duplicate character-mode card')
        seen.add(pair)
        return m[1]+('-1360' if pair[0]<0 else '173')+m[3]+m[4]+m[5]+'1240'+m[6]+'360'+m[7]
    text, count = re.subn(side, panel, text)
    if count != 6 or len(seen) != 6:
        raise ValueError('requires all six reviewed character-mode cards')
    description = (r'("rect"\s*:\s*\{\s*"x"\s*:\s*)(50|160)'
        r'(\s*,\s*"y"\s*:\s*)150(\s*,\s*"width"\s*:\s*)1255'
        r'(\s*,\s*"height"\s*:\s*)175(\s*\})')
    text, count = re.subn(description, lambda m: m[1]+('50' if m[2]=='50' else '110')+
        m[3]+'115'+m[4]+'1080'+m[5]+'225'+m[6], text)
    if count != 6:
        raise ValueError('requires all six reviewed character-mode descriptions')
    title = (r'("rect"\s*:\s*\{\s*"x"\s*:\s*)160'
             r'(\s*,\s*"y"\s*:\s*40\s*,\s*"width"\s*:\s*690\s*,\s*"height"\s*:\s*60\s*\})')
    text, count = re.subn(title, lambda m: m[1]+'110'+m[2], text)
    if count != 3:
        raise ValueError('requires all three reviewed right-hand titles')
    return text.encode('utf-8')


def overlay_asset(source, manifest, tvfs, name):
    offset, size = struct.unpack_from('>II', tvfs, 28)
    at = casc.cft_position(tvfs, offset, offset+size, manifest, name)
    key = bytes(tvfs[at:at+16]); length = struct.unpack_from('>I', tvfs, at+16)[0]
    position = int.from_bytes(key[11:16], 'big')
    with (source/'data'/casc.archive_filename(key[9],key[10])).open('rb') as stream:
        stream.seek(position); encoded = stream.read(length)
    if len(encoded) != length or casc.ekey_for(encoded,key[9],key[10],position) != key:
        raise ValueError('prepared texture CASC identity mismatch')
    spec = manifest[name][8]
    return zlib.decompress(encoded) if spec == 'z' else encoded if spec == 'n' else casc.read_data_file(source/'data',key,length)


def restore_player_textures(astc_work, source, manifest, tvfs, out, compiler, restore_menu=False):
    """Retain selected original ASTC mip chains; all other entries stay intact."""
    receipt = astc_work.parent/'verified.json'
    proof = strict_json(receipt.read_bytes())
    if (proof.get('passed') is not True or proof.get('game_build') != '90447' or
            proof.get('source_manifest_sha256') != sha256(source/'data/build_manifest.psv')):
        raise ValueError('character textures require verified ASTC work for this original build')
    cache = astc_work/'files'/CACHE
    if sha256(cache) != proof.get('files',{}).get(CACHE):
        raise ValueError('ASTC descriptor cache is not bound to its verification receipt')
    originals = strict_json(cache.read_bytes())
    entries = strict_json(overlay_asset(source,manifest,tvfs,CACHE))
    by_name = {logical_path(e['filename']):i for i,e in enumerate(entries)}
    if len(by_name) != len(entries):
        raise ValueError('duplicate prepared texture descriptor')
    native = NativeLayout(out/'work/character-native',compiler)
    payloads, sources, added = {}, {}, 0
    menu_textures = 0
    for original in originals:
        name = logical_path(original['filename'])
        menu = restore_menu and name.startswith('data/hd/env/texture/global/ui/')
        if not (name.startswith('data/hd/character/player/') or menu) or original['format'] not in FORMATS:
            continue
        if name not in by_name:
            raise ValueError('prepared player texture descriptor missing')
        current = entries[by_name[name]]
        if max(original['width'],original['height']) <= max(current['width'],current['height']):
            continue
        path = astc_work/'files'/name
        digest = sha256(path)
        if digest != proof.get('files',{}).get(name):
            raise ValueError('unverified original ASTC player texture: '+name)
        raw = path.read_bytes(); before = overlay_asset(source,manifest,tvfs,name)
        read_texture(raw,original,native); read_texture(before,current,native)
        for field in ('format','type','depth','layoutType'):
            if current[field] != original[field]:
                raise ValueError('unreviewed prepared player texture format')
        payloads[name] = raw; sources[name] = digest
        menu_textures += int(menu)
        entries[by_name[name]] = original
        added += len(raw)-len(before)
    if not payloads:
        raise ValueError('no reduced player textures found to restore')
    payloads[CACHE] = (json.dumps(entries,ensure_ascii=False,separators=(',',':'))+'\n').encode()
    report = dict(restored_textures=len(sources),added_texture_bytes=added,
        astc_receipt_sha256=sha256(receipt),astc_cache_sha256=sha256(cache),source_files=sources,
        other_texture_descriptors_unchanged=True,encoding_on_device=False)
    if restore_menu:
        report.update(restored_menu_textures=menu_textures,
                      classic_sprite_assets_unchanged=True,
                      menu_texture_prefix='data/hd/env/texture/global/ui/')
    return payloads, report


def character_scene(original, keep_background=False, keep_shadows=False):
    """Retain optional static scenery; the selected player is loaded separately."""
    if original.get('type') != 'Preset' or not isinstance(original.get('entities'), list):
        raise ValueError('reviewed menu preset required')
    result = copy.deepcopy(original)
    visual = {'ModelDefinitionComponent', 'VfxDefinitionComponent', 'DecalDefinitionComponent',
              'PrefabPlacementDefinitionComponent', 'SkeletonDefinitionComponent', 'PhysicsClothDefinitionComponent',
              'PointLightDefinitionComponent', 'SpotLightDefinitionComponent'}
    if keep_background:
        visual -= {'ModelDefinitionComponent', 'DecalDefinitionComponent',
                   'PrefabPlacementDefinitionComponent', 'PointLightDefinitionComponent',
                   'SpotLightDefinitionComponent'}
    kept, removed, terrain = [], [], 0
    for entity in result['entities']:
        components = entity.get('components')
        if not isinstance(components, list) or any(not isinstance(c, dict) or 'type' not in c for c in components):
            raise ValueError('unreviewed menu entity')
        types = {c['type'] for c in components}
        if 'TerrainDefinitionComponent' in types:
            terrain += 1
            models = [c for c in components if c['type'] == 'ModelDefinitionComponent']
            if len(models) != 1 or models[0].get('visibleLayers') != 1:
                raise ValueError('reviewed menu terrain root required')
            if not keep_background:
                models[0]['visibleLayers'] = 0
            kept.append(entity)
        elif types & visual:
            removed.append(entity.get('id'))
        else:
            kept.append(entity)
    if keep_background and not keep_shadows:
        for entity in kept:
            for component in entity['components']:
                if component['type'] == 'ModelDefinitionComponent':
                    component['shadowMask'] = 0
                    component['ghostShadows'] = False
    if terrain != 1 or not removed:
        raise ValueError('one terrain root and authored menu decoration required')
    result['entities'] = kept
    return result, dict(original_entities=len(original['entities']), retained_entities=len(kept),
                        decoration_entities_removed=len(removed), terrain_root_preserved=True,
                        static_background_preserved=keep_background,
                        player_loaded_by_unchanged_layout=True)


def light_creation_scene(original):
    """Keep authored character lighting and sound, omitting scenery preloads.

    The eight class actors belong to the unchanged HDCharacterSelectorWidget
    children, not this environment preset. Keep their positions and materials.
    """
    if original.get('type') != 'Preset' or not isinstance(original.get('entities'), list):
        raise ValueError('reviewed creation environment preset required')
    result = copy.deepcopy(original)
    lights = {'PointLightDefinitionComponent', 'SpotLightDefinitionComponent'}
    keep = lights | {'AudioEmitterComponent', 'AudioEnvironmentOverrideComponent'}
    kept, light_count = [], 0
    for entity in result['entities']:
        components = entity.get('components')
        if not isinstance(components, list) or any(not isinstance(c, dict) or 'type' not in c for c in components):
            raise ValueError('unreviewed creation environment entity')
        types = {c['type'] for c in components}
        if types & keep:
            if types - keep - {'TransformDefinitionComponent'}:
                raise ValueError('creation lighting/sound shares an unreviewed scenery component')
            light_count += len(types & lights)
            kept.append(entity)
    dependencies = result.get('dependencies')
    if not light_count or not isinstance(dependencies, dict) or any(not isinstance(v, list) for v in dependencies.values()):
        raise ValueError('creation lighting and dependency lists required')
    result['entities'] = kept
    result['dependencies'] = {k: v if k == 'json' else [] for k, v in dependencies.items()}
    return result, dict(original_entities=len(original['entities']), retained_entities=len(kept),
                        authored_lights_preserved=light_count,
                        environment_preload_entries_removed=sum(len(v) for k, v in dependencies.items() if k != 'json'),
                        class_widgets_unchanged=True)


def character_cameras(raw, keep_background=False, keep_shadows=False, menu_camera_fov=42):
    """Keep original camera/player activation; restrict only menu scene work."""
    if type(menu_camera_fov) is not int or not 38 <= menu_camera_fov <= 42:
        raise ValueError('menu camera FOV must be an integer from 38 to 42 degrees')
    if menu_camera_fov != 42 and not keep_background:
        raise ValueError('menu camera enlargement requires the retained background')
    text, seen = raw.decode('utf-8'), set()
    pattern = (r'("type"\s*:\s*"Camera3DWidget"\s*,\s*"name"\s*:\s*'
               r'"(Act[1-5]Camera)"\s*,\s*"fields"\s*:\s*\{)(.*?)(\n\s*\})')

    def edit(match):
        prefix, camera, body, suffix = match.groups()
        if camera in seen:
            raise ValueError('duplicate frontend camera: ' + camera)
        seen.add(camera)
        expected = 'CharacterSelect:SelectAct:Act' + camera[3]
        if len(re.findall(r'"activateOnMessage"\s*:\s*"'+re.escape(expected)+'"', body)) != 1:
            raise ValueError('unreviewed character camera activation')
        if camera == 'Act1Camera' and len(re.findall(r'"activateOnOpened"\s*:\s*true', body)) != 1:
            raise ValueError('initial character camera must remain active')
        if menu_camera_fov != 42:
            body, count = re.subn(r'("fovDegrees"\s*:\s*)42(?![\d.])',
                                 lambda m: m[1]+str(menu_camera_fov), body)
            if count != 1:
                raise ValueError('one original 42 degree camera FOV required: ' + camera)
        for edge in ('Min', 'Max'):
            bounds = re.findall(r'"playerBounds'+edge+r'"\s*:\s*(\[[^\]]+\])', body)
            if len(bounds) != 1:
                raise ValueError('one original player bound required: ' + camera)
            vector = json.loads(bounds[0])
            if len(vector) != 3 or any(type(v) not in (int, float) or not -10000 < v < 10000 for v in vector):
                raise ValueError('unreviewed player bounds')
            body, count = re.subn(r'("frustumBounds'+edge+r'"\s*:\s*)\[[^\]]+\]',
                                 lambda m: m[0] if keep_background else m[1]+bounds[0], body)
            if count != 1:
                raise ValueError('one original scene bound required: ' + camera)
        body, count = re.subn(r'"enableOutdoorShadow"\s*:\s*true',
                             lambda m: m[0] if keep_shadows else '"enableOutdoorShadow": false', body)
        if count != 1 or len(re.findall(r'"overrideFrustumBoundsOnActivate"\s*:\s*true', body)) != 1:
            raise ValueError('unreviewed original character camera policy')
        return prefix + body + suffix

    result, count = re.subn(pattern, edit, text, flags=re.S)
    if count != 5 or len(seen) != 5:
        raise ValueError('requires the five reviewed frontend cameras')
    player_pattern = (r'("type"\s*:\s*"PlayerLoaderWidget"\s*,\s*"name"\s*:\s*"Player"'
                      r'\s*,\s*"fields"\s*:\s*\{)(.*?)(\n\s*\})')
    player = list(re.finditer(player_pattern, result, re.S))
    if len(player) != 1 or '"captureInView"' in player[0][2] or '"animationSpeed"' in player[0][2]:
        raise ValueError('original selected-player display required')
    # The same widget already supports speed zero for the hidden preload actors.
    # Preserve the selected actor and gear; use a fixed idle pose in the menu.
    result = re.sub(player_pattern, lambda m: m[1]+'\n                "animationSpeed": 0,'+m[2]+m[3], result, flags=re.S)
    return result.encode('utf-8')


def without_class_preloads(raw, game_build='90447'):
    """Remove hidden actors without rewriting the selected actor or UI.

    Controller layouts inherit and redeclare these children. All four layouts
    must be prepared together, or a child override recreates a removed actor.
    """
    pattern = (rb'^        \{\r?\n            "type": "PlayerLoaderWidget", '
               rb'"name": "Preload([A-Za-z]+)",\r?\n.*?^        \},\r?\n')
    expected = {'Amazon', 'Sorceress', 'Necromancer', 'Paladin', 'Barbarian', 'Druid', 'Assassin'}
    if game_build not in ('90447', '94012'):
        raise ValueError('unsupported character preload build')
    if game_build == '94012':
        expected.add('Warlock')
    seen = set()

    def remove(match):
        name = match[1].decode()
        if (name not in expected or name in seen or b'"children"' in match[0] or
                match[0].count(b'"type"') != 1):
            raise ValueError('unreviewed hidden character preload widget')
        seen.add(name)
        return b''

    result, count = re.subn(pattern, remove, raw, flags=re.M | re.S)
    if count != len(expected) or seen != expected or b'"Preload' in result or b'"name": "Player"' not in result:
        raise ValueError('reviewed preloads and the retained selected player required')
    return result


def light_menu_player(original):
    """Add a bounded Act-1 menu fill using the authored Act-2 player-light power."""
    result=copy.deepcopy(original);changed=[]
    for entity in result['entities']:
        for component in entity['components']:
            if component.get('type')!='VisualDataComponent':continue
            if component.get('exposureValue')!=4.0 or component.get('playerUnitLightPower')!=0.0:
                raise ValueError('reviewed Act-1 menu exposure/player light required')
            for field,after in (('exposureValue',3.0),('playerUnitLightPower',2125.0)):
                changed.append(dict(field=field,before=component[field],after=after));component[field]=after
    if len(changed)!=2:raise ValueError('one reviewed Act-1 menu visual component required')
    return result,changed


def disable_cameras(raw, no_player_capture=False):
    """Edit only camera activation fields; retain all other layout bytes."""
    text, seen = raw.decode('utf-8'), set()
    pattern = (r'("type"\s*:\s*"Camera3DWidget"\s*,\s*"name"\s*:\s*'
               r'"(Act[1-5]Camera)"\s*,\s*"fields"\s*:\s*\{)(.*?)(\n\s*\})')

    def edit(match):
        prefix, camera, body, suffix = match.groups()
        if camera in seen:
            raise ValueError('duplicate frontend camera: ' + camera)
        seen.add(camera)
        expected = 'CharacterSelect:SelectAct:Act' + camera[3]
        field = r'"activateOnMessage"\s*:\s*"' + re.escape(expected) + '"'
        body, count = re.subn(field, '"activateOnMessage": ""', body)
        if count != 1:
            raise ValueError('unreviewed camera activation: ' + camera)
        if camera == 'Act1Camera':
            body, count = re.subn(r'"activateOnOpened"\s*:\s*true',
                                 '"activateOnOpened": false', body)
            if count != 1:
                raise ValueError('unreviewed initial camera activation')
        return prefix + body + suffix

    result, count = re.subn(pattern, edit, text, flags=re.S)
    if count != 5 or seen != {'Act%dCamera' % i for i in range(1, 6)}:
        raise ValueError('requires the five reviewed frontend cameras')
    if no_player_capture:
        player = (r'("type"\s*:\s*"PlayerLoaderWidget"\s*,\s*"name"\s*:\s*"Player"'
                  r'\s*,\s*"fields"\s*:\s*\{)(.*?)(\n\s*\})')
        def exclude(match):
            if '"captureInView"' in match[2]:
                raise ValueError('unreviewed selected-player capture view')
            # Reuse the original preload widgets' non-display capture view.
            return match[1]+'\n                "captureInView": "$INT_MAX",'+match[2]+match[3]
        result, count = re.subn(player, exclude, result, flags=re.S)
        if count != 1:
            raise ValueError('one selected-player loader required')
    return result.encode('utf-8')


def prepare(base, prepared, out, no_rain=False, no_player_capture=False, keep_character=False, astc_work=None, compiler='cc', keep_background=False, restore_menu_textures=False, keep_menu_shadows=False, fit_handheld_modes=False, menu_camera_fov=42, selected_character_only=False, game_build="90447", light_creation=False):
    if game_build not in ('90447', '94012'):
        raise ValueError('unsupported frontend build')
    if light_creation and not (game_build == '94012' and keep_character and selected_character_only and fit_handheld_modes):
        raise ValueError('--light-creation requires the 94012 selected-character handheld preset')
    if game_build == '94012' and (keep_background or no_rain or
            keep_character and not (selected_character_only and fit_handheld_modes)):
        raise ValueError('94012 character menus require selected-character-only handheld layout without scenery')
    layout_sha = LAYOUT_SHA256 if game_build == '90447' else '2380b578085b918123d55be8731069f9446f85032e0eeb7716d8364a2db59b79'
    options_sha = CHARACTER_OPTIONS_SHA256 if game_build == '90447' else '59f80e3a236424bb26ab542d3f24db7a3c4352aff3734246e628162a4b3dfea3'
    if keep_character and no_player_capture:
        raise ValueError('cannot preserve and disable the selected-player preview together')
    if selected_character_only and not keep_character:
        raise ValueError('--selected-character-only requires --keep-character')
    if keep_background and not keep_character:
        raise ValueError('--keep-background requires --keep-character')
    if restore_menu_textures and (not keep_background or astc_work is None):
        raise ValueError('--restore-menu-textures requires --keep-background and --astc-work')
    if keep_menu_shadows and not keep_background:
        raise ValueError('--keep-menu-shadows requires --keep-background')
    if fit_handheld_modes and not (keep_character or no_player_capture):
        raise ValueError('--fit-handheld-modes requires a reviewed character or disabled-preview preset')
    if fit_handheld_modes and keep_character and not keep_background and not selected_character_only:
        raise ValueError('character-only handheld layout requires --selected-character-only')
    if (type(menu_camera_fov) is not int or not 38 <= menu_camera_fov <= 42 or
            menu_camera_fov != 42 and not keep_background):
        raise ValueError('--menu-camera-fov requires 38..42 degrees and --keep-background')
    base, prepared, out = base.resolve(), prepared.resolve(), out.resolve()
    if astc_work is not None:
        astc_work = astc_work.resolve()
        if not keep_character:
            raise ValueError('--astc-work is for the retained-character preset')
    inputs = (base,prepared) if astc_work is None else (base,prepared,astc_work)
    if out.exists() or any(p == out or p in out.parents or out in p.parents for p in inputs):
        raise ValueError('choose a new, separate private output directory')
    install(prepared, base, check_only=True)
    manifest_raw = (prepared / 'manifest.json').read_bytes()
    proof = json.loads(manifest_raw)
    manifest = casc.read_manifest(base)
    raw = read_asset(base, manifest, LAYOUT)
    if hashlib.sha256(raw).hexdigest() != layout_sha:
        raise ValueError('requires the reviewed original 90447 frontend layout')
    layout = character_cameras(raw,keep_background,keep_menu_shadows,menu_camera_fov) if keep_character else disable_cameras(raw,no_player_capture)
    if selected_character_only:
        layout = without_class_preloads(layout, game_build)
    payloads, sources = {LAYOUT: layout}, {LAYOUT: layout_sha}
    if selected_character_only:
        for name, expected in (PRELOAD_LAYOUT_SOURCES_94012 if game_build == '94012' else PRELOAD_LAYOUT_SOURCES).items():
            original = read_asset(base, manifest, name)
            if hashlib.sha256(original).hexdigest() != expected:
                raise ValueError('unreviewed inherited frontend layout: ' + name)
            payloads[name] = without_class_preloads(original, game_build)
            sources[name] = expected
    if fit_handheld_modes:
        original = read_asset(base, manifest, CHARACTER_OPTIONS)
        if hashlib.sha256(original).hexdigest() != options_sha:
            raise ValueError('unreviewed original character-mode layout')
        payloads[CHARACTER_OPTIONS] = fit_character_options(original)
        sources[CHARACTER_OPTIONS] = options_sha
        if game_build == '94012':
            original = read_asset(base, manifest, CHARACTER_STATS)
            if hashlib.sha256(original).hexdigest() != CHARACTER_STATS_94012_SHA256:
                raise ValueError('unreviewed original controller character sheet')
            payloads[CHARACTER_STATS] = fit_character_stats(original)
            sources[CHARACTER_STATS] = CHARACTER_STATS_94012_SHA256
    menu_changes = {'hidden_class_preload_definitions_removed': 32 if game_build == '94012' else 28} if selected_character_only else {}
    if keep_character:
        for name, expected in (CHARACTER_SOURCES_94012 if game_build == '94012' else CHARACTER_SOURCES).items():
            value = read_asset(base, manifest, name)
            if hashlib.sha256(value).hexdigest() != expected:
                raise ValueError('unreviewed original menu scenery: ' + name)
            original = strict_json(value)
            if name == SCENE:
                changed, menu_changes['scene'] = character_scene(original,keep_background,keep_menu_shadows)
            elif name == BIOME:
                changed, changes = cut_biome(original, 0)
                menu_changes['biome_density_fields'] = len(changes)
            else:
                changed, changes = cut_visual(original, not keep_menu_shadows, True)
                if keep_background and name=='data/hd/env/vis/ui_frontend_act1_vignette.json':
                    changed,light_changes=light_menu_player(changed);changes.extend(light_changes)
                for entity in changed['entities']:
                    for component in entity['components']:
                        if component.get('type') == 'VisualDataComponent':
                            for field in (() if keep_background else ('fogDensity', 'depthFogDensity', 'heightFogDensity')):
                                if field in component:
                                    before = component[field]
                                    if type(before) not in (int, float) or not 0 <= before < 1000:
                                        raise ValueError('unreviewed menu fog density')
                                    component[field] = 0.0
                                    changes.append(dict(field=field, before=before, after=0.0))
                menu_changes[name] = changes
            payloads[name] = (json.dumps(changed, ensure_ascii=False, separators=(',', ':'), allow_nan=False)+'\n').encode()
            sources[name] = expected
    if light_creation:
        for name, expected in CREATION_SOURCES_94012.items():
            value = read_asset(base, manifest, name)
            if hashlib.sha256(value).hexdigest() != expected:
                raise ValueError('unreviewed original creation asset: ' + name)
            if name == CREATION_LAYOUT:
                changed, count = re.subn(rb'"enableOutdoorShadow"\s*:\s*true', b'"enableOutdoorShadow": false', value)
                if count != 1:
                    raise ValueError('one original creation camera shadow switch required')
                payloads[name] = changed
            else:
                original = strict_json(value)
                if name == CREATION_SCENE:
                    changed, menu_changes['creation_scene'] = light_creation_scene(original)
                else:
                    changed, changes = cut_biome(original, 0)
                    menu_changes['creation_biome_density_fields'] = len(changes)
                payloads[name] = (json.dumps(changed, ensure_ascii=False, separators=(',', ':'), allow_nan=False)+'\n').encode()
            sources[name] = expected
    if no_rain:
        original = {n: read_asset(base, manifest, n) for n in SOURCE_HASHES}
        if any(hashlib.sha256(v).hexdigest() != SOURCE_HASHES[n] for n, v in original.items()):
            raise ValueError('unreviewed original weather tables')
        text, binary, _ = disable_rain(*(original[n] for n in SOURCE_HASHES))
        payloads.update(dict(zip(SOURCE_HASHES, (text, binary))))
        sources.update(SOURCE_HASHES)
    out.mkdir(parents=True, mode=0o700)
    source = out / 'work/source'
    (source / 'data').mkdir(parents=True)
    for directory in (base / 'data', prepared / 'RomFS/data'):
        for path in directory.iterdir():
            if path.is_file():
                target = source / 'data' / path.name
                if target.is_symlink():
                    target.unlink()
                target.symlink_to(path)
    tvfs = casc.load_vfs1(source)
    if astc_work is not None:
        textures, menu_changes['player_textures'] = restore_player_textures(astc_work,source,manifest,tvfs,out,compiler,restore_menu_textures)
        payloads.update(textures)
    offset, size = struct.unpack_from('>II', tvfs, 28)
    positions = {n: casc.cft_position(tvfs, offset, offset + size, manifest, n) for n in payloads}
    replacements = {}
    for i, (name, value) in enumerate(payloads.items()):
        path = out / 'work' / ('asset-%d.bin' % i)
        path.write_bytes(value)
        replacements[name] = path
    rebuilt = out / 'work/rebuilt'
    casc.make_overlay(source, rebuilt, replacements)
    tvfs = casc.load_vfs1(rebuilt)
    for name, at in positions.items():
        key = bytes(tvfs[at:at + 16])
        size = struct.unpack_from('>I', tvfs, at + 16)[0]
        offset = int.from_bytes(key[11:16], 'big')
        with (rebuilt / 'data' / casc.archive_filename(key[9],key[10])).open('rb') as stream:
            stream.seek(offset)
            encoded = stream.read(size)
        spec = manifest[name][8]
        decoded = zlib.decompress(encoded) if spec == 'z' else encoded if spec == 'n' else casc.read_data_file(rebuilt / 'data', key, size)
        if casc.ekey_for(encoded, key[9], key[10], offset) != key or decoded != payloads[name]:
            raise ValueError('generated CASC round-trip mismatch: ' + name)
    target = out / 'overlay/data'
    target.mkdir(parents=True)
    for path in (rebuilt / 'data').iterdir():
        if path.is_file() and not path.is_symlink():
            shutil.copyfile(path, target / path.name)
    files = {'overlay/data/' + p.name: dict(size=p.stat().st_size, sha256=sha256(p)) for p in target.iterdir()}
    if len(files) != 2 or 'overlay/data/.build.config' not in files:
        raise ValueError('unexpected generated overlay layout')
    if (prepared / 'manifest.json').read_bytes() != manifest_raw:
        raise ValueError('source prepared manifest changed')
    report = dict(schema=1, complete=True, game_build=game_build,
        policy='classic-frontend-character-v6' if light_creation else 'classic-frontend-no-preview-v3' if no_player_capture and fit_handheld_modes else 'classic-frontend-character-v5' if keep_character and not keep_background and selected_character_only and fit_handheld_modes else 'classic-frontend-character-v4' if restore_menu_textures or keep_menu_shadows or fit_handheld_modes else 'classic-frontend-character-v3' if keep_background else CHARACTER_POLICY if keep_character else 'classic-frontend-no-preview-v2' if no_player_capture else POLICY,
        player_capture_disabled=no_player_capture,
        no_preview=not keep_character, tight_character_bounds=keep_character and not keep_background,
        background_scene_cut=keep_character and not keep_background,
        static_background_preserved=keep_background, static_player_pose=keep_character, menu_changes=menu_changes,
        selected_character_only=selected_character_only,
        light_creation=light_creation,
        menu_shadows_preserved=keep_menu_shadows,
        menu_camera_fov_degrees=menu_camera_fov if keep_character else None,
        handheld_mode_layout=fit_handheld_modes,
        handheld_character_sheet=fit_handheld_modes and game_build == '94012',
        classic_no_rain=no_rain, source_layout_sha256=layout_sha,
        source_assets_sha256=sources, source_manifest_sha256=proof['source_manifest_sha256'],
        source_prepared_manifest_sha256=hashlib.sha256(manifest_raw).hexdigest(),
        source_prepared_files={r['path']: r for r in proof['files'] if r['path'].startswith('RomFS/data/')},
        output_files=files, roundtrip_verified=True, original_files_modified=False,
        render_dimensions_changed=False, runtime_rendering_verified=False, target_fps_verified=False)
    (out / 'complete.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game-build', choices=('90447', '94012'), default='90447')
    p.add_argument('--romfs', type=Path, required=True)
    p.add_argument('--prepared', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--classic-no-rain', action='store_true', help='also include the verified classic no-rain tables')
    preview = p.add_mutually_exclusive_group()
    preview.add_argument('--disable-player-capture', action='store_true', help='legacy experiment: hide the selected character as well as scenery')
    preview.add_argument('--keep-character', action='store_true', help='retain the selected character and original cameras; use tight scene bounds and no outdoor menu shadows')
    p.add_argument('--keep-background', action='store_true', help='with --keep-character, retain scenery/terrain/authored lighting; omit dynamic FX/cloth; shadows are separately opt-in')
    p.add_argument('--selected-character-only', action='store_true', help='with --keep-character, remove hidden class preloads from all four inherited menu layouts; retain the selected actor and original cleanup')
    p.add_argument('--light-creation', action='store_true', help='94012 selected-character preset: omit creation scenery and preloads, retaining all eight class actors and their authored lighting')
    p.add_argument('--astc-work', type=Path, help='verified prepare_game.py 11-astc/work; restore original player textures')
    p.add_argument('--restore-menu-textures', action='store_true', help='with retained background and verified ASTC work, restore original menu sky/scenery mip chains')
    p.add_argument('--keep-menu-shadows', action='store_true', help='with retained background, retain the original menu camera, model and directional shadow settings')
    p.add_argument('--fit-handheld-modes', action='store_true', help='fit character-mode cards and the 94012 character sheet to 4:3 and wider handheld displays; preserve controls and text')
    p.add_argument('--cc', default='cc', help='existing host C compiler for native texture-layout validation')
    p.add_argument('--menu-camera-fov', type=int, default=42, choices=range(38,43),
                   help='retained menu camera FOV in degrees; 39 enlarges the original scene about 8%%; UI and output dimensions stay fixed')
    a = p.parse_args()
    result = prepare(a.romfs, a.prepared, a.out, a.classic_no_rain,a.disable_player_capture,a.keep_character,a.astc_work,a.cc,a.keep_background,a.restore_menu_textures,a.keep_menu_shadows,a.fit_handheld_modes,a.menu_camera_fov,a.selected_character_only,a.game_build,a.light_creation)
    print(json.dumps(dict(complete=True, policy=result['policy'], output=str(a.out),
        copy_only=['overlay/', 'complete.json'], game_started=False)))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, struct.error) as error:
        sys.exit('classic frontend preparation: ' + str(error))
