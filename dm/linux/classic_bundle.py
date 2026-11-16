#!/usr/bin/env python3
# INPUT: A verified private classic bundle and an idle user profile.
# OUTPUT: Fixed resource paths and first-install visual defaults; existing saves stay intact.
# POS: Shared preparation/install contract; no game files, keys or device probing.
import json
import os
from pathlib import Path
import tempfile
import time
from device_profiles import load_device

PATHS = dict(modules='classic-native-v6', shaders='classic-shaders/shaders.squashfs',
             frontend='classic-frontend', settings='classic-defaults.json')
LEGACY_DEFAULTS = {name: 0 for name in ('Texture Quality', 'Shadow Quality',
    'Ambient Occlusion Quality', 'Anti Aliasing', 'Vfx Lighting Quality',
    'Atmospherics Quality', 'Transparency Quality', 'Vfx Quality',
    'Character Detail', 'Environment Detail', 'Texture Anisotropy')}
LEGACY_DEFAULTS.update({'Light Quality': 2, 'Blended Shadows': 1, 'Perspective': 1,
                 'Legacy Graphics Mode': 1, 'Game Resolution': 1, 'Low Vision Mode': 1})
# Stored enum values include the original renderer's offsets: texture/detail
# High=4 (UI 2), shadow Medium=2 (UI 1), anisotropy 4x=2.
DEFAULTS = dict(LEGACY_DEFAULTS, **{'Texture Quality': 4, 'Shadow Quality': 2,
    'Ambient Occlusion Quality': 1, 'Character Detail': 4, 'Environment Detail': 4,
    'Texture Anisotropy': 2, 'Legacy Graphics Mode': 2})


def runtime_game_build(binary):
    marker=b'D2R-LINUX-GAME-BUILD='
    builds=[build for build in ('90447','94012') if marker+build.encode()+b'\0' in binary]
    if len(builds)==1:
        return builds[0]
    if not builds and marker not in binary:
        return '90447'  # Public runtimes before the explicit build tag.
    raise ValueError('ambiguous or unsupported runtime game build')


def legacy_descriptor():
    return dict(schema=1, game_build='90447', preset='rk3566-classic-quality-v3',
                paths=PATHS, output_extent=[960, 720], classic_world_extent=[800, 600],
                menu_preview=True, menu_background=True, menu_scene_cache=True, classic_no_rain=True)


def descriptor(device=None, game_build="90447"):
    if game_build not in ("90447", "94012") or game_build == "94012" and (device is None or device["id"] != "rk3566-2gb"):
        raise ValueError("94012 requires the flat classic device preset")
    if device is not None:
        if load_device(device['id'])!=device:
            raise ValueError('unreviewed device bundle')
        result=dict(schema=2,game_build=game_build,preset=device['preset'],paths=PATHS,
            device_id=device['id'],device_profile_sha256=device['profile_sha256'],
            output_extent=[960,720],classic_world_extent=[800,600],menu_preview=device['settings']['keep_character'],
            menu_background=False,menu_scene_cache=device['settings']['keep_character'],
            selected_character_only=device['settings']['selected_character_only'],
            classic_no_rain=False,menu_shadows=False,menu_mode_layout=True)
        if game_build=='94012':
            result.update(output_extent=None,output_policy='sdl-drawable-aspect-v1',output_limit=[1920,1080])
        return result
    return dict(schema=1, game_build=game_build, preset='rk3566-classic-quality-v4',
                paths=PATHS, output_extent=[960, 720], classic_world_extent=[800, 600],
                menu_preview=True, menu_background=True, menu_scene_cache=True, classic_no_rain=False,
                menu_shadows=True, menu_original_textures=True, menu_mode_layout=True)


def compact_descriptor(device, game_build="90447"):
    if device['id'] != 'rk3566-2gb':
        raise ValueError('compact resources require the flat 2 GB preset')
    return dict(descriptor(device, game_build), schema=3, compact_resources='classic-zhcn-enus-v1')


def load_bundle(game, manifest):
    """Only the fixed software-defined paths may select launcher resources."""
    report = json.loads((game/'classic-bundle.json').read_text())
    if not isinstance(report,dict):
        raise ValueError('classic bundle descriptor must be an object')
    device=None
    compact = report.get('schema') == 3
    if report.get('schema') in (2, 3):
        device=load_device(report.get('device_id'))
        if (report!=(compact_descriptor(device, manifest.get("game_build","90447")) if compact else descriptor(device, manifest.get("game_build","90447"))) or 'device-profile.json' not in manifest['files'] or
                json.loads((game/'device-profile.json').read_text())!=device):
            raise ValueError('classic bundle device profile mismatch')
    expected = DEFAULTS if device or report == descriptor() else LEGACY_DEFAULTS if report == legacy_descriptor() else None
    if expected is None:
        raise ValueError('unsupported classic bundle descriptor')
    required = {'classic-bundle.json', PATHS['settings'], PATHS['shaders'],
                PATHS['modules']+'/classic-native.json', PATHS['frontend']+'/complete.json'}
    if not required <= manifest['files'].keys():
        raise ValueError('classic bundle resources are missing from the game manifest')
    defaults = json.loads((game/PATHS['settings']).read_text())
    if defaults != expected or any(type(v) is not int for v in defaults.values()):
        raise ValueError('unreviewed classic quality defaults')
    result={name: game/path for name, path in PATHS.items()}
    if device:result['_device']=device
    if compact:
        from compact_classic import verify_compact
        verify_compact(game, manifest, device)
        result['_compact']=True
    return result


def seed_settings(profile, defaults=None):
    """Caller holds session.lock. Seed a new Settings pair, never merge existing preferences."""
    defaults = DEFAULTS if defaults is None else defaults
    if defaults not in (DEFAULTS, LEGACY_DEFAULTS) or any(type(v) is not int for v in defaults.values()):
        raise ValueError('unreviewed classic quality defaults')
    directory = profile/'save/Settings'
    if directory.is_symlink():
        raise ValueError('linked Settings directory is not supported')
    directory.mkdir(parents=True, exist_ok=True)
    target, meta = directory/'Settings.json', directory/'Settings.json.meta'
    if target.is_symlink() or meta.is_symlink():
        raise ValueError('linked settings files are not supported')
    if target.exists():
        if not target.is_file():
            raise ValueError('Settings.json is not a regular file')
        return False
    if meta.exists():
        raise ValueError('orphan settings metadata; existing profile was not changed')
    pending, created = [], []
    try:
        for path, data in ((meta, {'last_modified_time': time.time_ns()}), (target, defaults)):
            fd, name = tempfile.mkstemp(prefix='.classic-defaults-', dir=directory)
            pending.append(Path(name))
            with os.fdopen(fd, 'w') as stream:
                json.dump(data, stream, indent=2); stream.write('\n')
                stream.flush(); os.fsync(stream.fileno())
            os.link(name, path)  # Exclusive publication; never replace a user file.
            created.append(path)
        return True
    except BaseException:
        for path in reversed(created):
            path.unlink()
        raise
    finally:
        for path in pending:
            path.unlink(missing_ok=True)
