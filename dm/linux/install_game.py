#!/usr/bin/env python3
# INPUT: User's prepared game tree, installed public runtime and PortMaster paths.
# OUTPUT: Verified overlay and a launcher rendered for this device's directories.
# POS: Device installation; optional fresh-profile defaults, no existing save edits.
"""Run on the handheld after copying 13-device/game and the public runtime."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import shlex
import subprocess
import struct
import sys
from classic_bundle import load_bundle, seed_settings, runtime_game_build
from device_profiles import install_paths, launcher_options, verify_components


def verify_game(game):
    manifest = json.loads((game / 'game-manifest.json').read_text(encoding='utf-8'))
    if manifest.get('game_build') not in ('90447','94012') or not manifest.get('files'):
        raise ValueError('missing/private game manifest for build 90447')
    for name, expected in manifest['files'].items():
        relative = PurePosixPath(name)
        if (relative.is_absolute() or '..' in relative.parts or '\\' in name or
                not (game / name).resolve().is_relative_to(game.resolve())):
            raise ValueError('unsafe game manifest path')
        path = game / name
        if path.is_symlink() or path.stat().st_size != expected['size']:
            raise ValueError('game payload size/type mismatch: ' + name)
        h = hashlib.sha256()
        with path.open('rb') as stream:
            for chunk in iter(lambda: stream.read(8 << 20), b''):
                h.update(chunk)
        if h.hexdigest() != expected['sha256']:
            raise ValueError('game payload hash mismatch: ' + name)
    return manifest


def render(template, values):
    for token, path in values.items():
        value = str(path)
        if not path.is_absolute() or '\n' in value or '\r' in value:
            raise ValueError('launcher paths must be absolute, with no newlines')
        if template.count(token) != 1:
            raise ValueError('launcher template token mismatch: ' + token)
        template = template.replace(token, shlex.quote(value))
    if '@D2R_' in template:
        raise ValueError('unresolved launcher template')
    return template


def verify_classic_modules(directory):
    report=json.loads((directory/'classic-native.json').read_text())
    game_build=report.get('game_build','90447')
    build_ids={'90447':'c5e0c2db39a8d8a3ae2d5dfaaa68e16e','94012':'db0507f36b84200ac05f75d76ed5026f'}
    allowed={n+'.'+s for n in ('main','sdk','subsdk0') for s in
             ('patched.bin','fixups.tsv','gates.tsv','params.tsv','sites.tsv','cuts.tsv','obs.tsv')}
    if (not report.get('complete') or not report.get('runtime_validator_passed') or
            game_build not in build_ids or report.get('build_id')!=build_ids[game_build] or
            set(report.get('output_files',{}))!=allowed):
        raise ValueError('requires complete private classic modules for build 90447')
    for name in sorted(allowed):
        path=directory/name;expected=report['output_files'][name]
        if path.is_symlink() or not path.is_file() or path.stat().st_size!=expected['bytes']:
            raise ValueError('classic module size/type mismatch: '+name)
        if hashlib.sha256(path.read_bytes()).hexdigest()!=expected['sha256']:
            raise ValueError('classic module hash mismatch: '+name)
    path=directory/'main.patched.bin';size=path.stat().st_size
    if size<0x3078000 or size>64*1024*1024 or size%0x4000:
        raise ValueError('invalid classic module size')
    with path.open('rb') as stream:
        stream.seek(size-0x4000-256);header=stream.read(32)
    magic,version,sites,source,control,_=struct.unpack('<8sIIQII',header)
    if magic!=(b'D2RHD940' if game_build=='94012' else b'D2RHD904') or version!=6 or sites!=(3 if game_build=='94012' else 8) or control!=size-0x4000:
        raise ValueError('requires native classic format 6; format 5 was withdrawn')
    return report


def classic_shared_pool_minimum(report, runtime_binary):
    """Keep both selected GPU pools shared; require fixes before lowering the floor."""
    policy=report.get('graphics_pool_policy') or {}
    if not isinstance(policy,dict):
        raise ValueError('invalid prepared graphics pool capacities')
    buffer_mib=policy.get('buffer_pool_mib',400)
    texture_mib=policy.get('texture_pool_mib',710)
    if any(type(value) is not int or not 64<=value<=maximum
           for value,maximum in ((buffer_mib,400),(texture_mib,710))):
        raise ValueError('invalid prepared graphics pool capacities')
    minimum=min(384,buffer_mib,texture_mib)
    if minimum<=260 and b'LINUX_SHARED_POOL_SKIP_COMPRESSIBLE' not in runtime_binary:
        raise ValueError('smaller pools require the C89 runtime with flags-aware shared pools')
    if report.get('terrain_quality_policy') and b'LINUX_3D_SCALE_WARMUP' not in runtime_binary:
        raise ValueError('terrain preparation requires the C89 runtime with original-rebuild warmup')
    return minimum


def file_sha256(path):
    digest=hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(4<<20),b''):digest.update(chunk)
    return digest.hexdigest()


def shader_pipeline_cache_directory(image, root=None):
    """Keep driver cache separate for each actual Shader bank."""
    if image.is_symlink() or not image.is_file():
        raise ValueError('regular shader image required for cache identity')
    return (root if root is not None else image.parent/'pipeline-cache')/file_sha256(image)


def verify_classic_overlay(directory,prepared,policy):
    report=json.loads((directory/'complete.json').read_text())
    files=report.get('output_files',{})
    if (report.get('schema')!=1 or report.get('complete') is not True or
            report.get('game_build') not in ('90447','94012') or report.get('policy')!=policy or
            report.get('roundtrip_verified') is not True or len(files)!=2 or
            'overlay/data/.build.config' not in files or
            any(n!='overlay/data/.build.config' and not re.fullmatch(r'overlay/data/[0-9a-f]{2}-[0-9a-f]{8}\.data',n) for n in files)):
        raise ValueError('requires a complete computer-prepared 90447 classic overlay')
    if file_sha256(prepared/'manifest.json')!=report.get('source_prepared_manifest_sha256'):
        raise ValueError('weather overlay was prepared for another base pack')
    source_manifest=json.loads((prepared/'manifest.json').read_text())
    if source_manifest.get('game_build','90447')!=report['game_build']:
        raise ValueError('overlay/prepared game build mismatch')
    sources={r['path']:r for r in source_manifest['files'] if r['path'].startswith('RomFS/data/')}
    if sources!=report.get('source_prepared_files'):
        raise ValueError('weather overlay source manifest mismatch')
    for name,expected in {**sources,**files}.items():
        root=prepared if name in sources else directory
        path=root/name
        if (path.is_symlink() or not path.resolve().is_relative_to(root.resolve()) or
                not path.is_file() or path.stat().st_size!=expected.get('size') or
                file_sha256(path)!=expected.get('sha256')):
            raise ValueError('weather overlay/source checksum mismatch: '+name)
    return directory/'overlay'


def verify_classic_weather(directory,prepared):
    return verify_classic_overlay(directory,prepared,'no-rain-v1')


def verify_classic_frontend(directory,prepared):
    report=json.loads((directory/'complete.json').read_text())
    policy=report.get('policy')
    if policy not in ('classic-frontend-no-preview-v1','classic-frontend-no-preview-v2','classic-frontend-no-preview-v3',
                       'classic-frontend-character-v1','classic-frontend-character-v2','classic-frontend-character-v3','classic-frontend-character-v4','classic-frontend-character-v5','classic-frontend-character-v6'):
        raise ValueError('unreviewed classic frontend policy')
    if policy=='classic-frontend-no-preview-v2' and report.get('player_capture_disabled') is not True:
        raise ValueError('frontend v2 requires the explicit selected-player capture cut')
    if policy=='classic-frontend-no-preview-v3' and not (report.get('player_capture_disabled') is True and report.get('handheld_mode_layout') is True):
        raise ValueError('flat frontend v3 requires disabled preview and handheld controls')
    if policy.startswith('classic-frontend-character-'):
        if (report.get('no_preview') is not False or report.get('player_capture_disabled') is not False or
                report.get('tight_character_bounds') is not (policy not in ('classic-frontend-character-v3','classic-frontend-character-v4'))):
            raise ValueError('character frontend requires the original player display and reviewed camera bounds')
        if policy.endswith('-v2') and report.get('background_scene_cut') is not True:
            raise ValueError('character frontend v2 requires its scenery cut')
        if policy.endswith(('-v5','-v6')) and not (report.get('background_scene_cut') is True and
                report.get('static_background_preserved') is False and
                report.get('selected_character_only') is True and
                report.get('handheld_mode_layout') is True):
            raise ValueError('character frontend v5/v6 requires no scenery, one selected actor and handheld controls')
        if policy.endswith('-v6') and not (report.get('game_build') == '94012' and report.get('light_creation') is True):
            raise ValueError('character frontend v6 requires the reviewed light 94012 creation scene')
        if policy.endswith(('-v3','-v4')) and (report.get('background_scene_cut') is not False or
                report.get('static_background_preserved') is not True):
            raise ValueError('character frontend v3/v4 requires retained static scenery')
        if policy.endswith('-v4'):
            textures=report.get('menu_changes',{}).get('player_textures',{}).get('restored_menu_textures',0)
            if not (report.get('menu_shadows_preserved') is True or type(textures) is int and textures>0 or
                    report.get('handheld_mode_layout') is True):
                raise ValueError('character frontend v4 requires explicit menu quality or handheld-layout preparation')
    elif report.get('no_preview') is not True:
        raise ValueError('legacy frontend requires its explicit no-preview policy')
    overlay=verify_classic_overlay(directory,prepared,policy)
    layout_hashes={'90447':'1d76dc8a77a3c80ce0592ff90331430f68a0fc878032ee2d22bfdaba781916ea',
                   '94012':'2380b578085b918123d55be8731069f9446f85032e0eeb7716d8364a2db59b79'}
    if report.get('game_build')=='94012' and (policy not in ('classic-frontend-no-preview-v3','classic-frontend-character-v5','classic-frontend-character-v6') or
            report.get('handheld_character_sheet') is not True):
        raise ValueError('94012 requires the reviewed handheld frontend and character sheet')
    if report.get('source_layout_sha256')!=layout_hashes.get(report.get('game_build')):
        raise ValueError('requires the reviewed original frontend layout')
    return overlay


def verify_remaster_quality(directory,prepared):
    report=json.loads((directory/'complete.json').read_text())
    files=report.get('output_files',{})
    if (report.get('schema')!=1 or report.get('complete') is not True or
            report.get('game_build')!='90447' or report.get('policy')!='remaster-authored-visual-cuts-v1' or
            report.get('roundtrip_verified') is not True or len(files)!=2 or
            'overlay/data/.build.config' not in files or
            any(n!='overlay/data/.build.config' and not re.fullmatch(r'overlay/data/[0-9a-f]{2}-[0-9a-f]{8}\.data',n) for n in files)):
        raise ValueError('requires a complete computer-prepared 90447 remaster quality overlay')
    if file_sha256(prepared/'manifest.json')!=report.get('source_prepared_manifest_sha256'):
        raise ValueError('remaster overlay was prepared for another base pack')
    source_manifest=json.loads((prepared/'manifest.json').read_text())
    if source_manifest.get('source_manifest_sha256')!=report.get('source_manifest_sha256'):
        raise ValueError('remaster overlay original-game identity mismatch')
    sources={r['path']:r for r in source_manifest['files'] if r['path'].startswith('RomFS/data/')}
    for name,expected in {**sources,**files}.items():
        root=prepared if name in sources else directory
        path=root/name
        if (path.is_symlink() or not path.resolve().is_relative_to(root.resolve()) or
                not path.is_file() or path.stat().st_size!=expected.get('size') or
                file_sha256(path)!=expected.get('sha256')):
            raise ValueError('remaster overlay/source checksum mismatch: '+name)
    return directory/'overlay'


def verify_classic_hd_suppression(directory,prepared):
    """Accept only original-size, classic-only definition suppression packs."""
    report=json.loads((directory/'complete.json').read_text())
    files=report.get('output_files',{})
    if (report.get('schema')!=1 or report.get('complete') is not True or
            report.get('game_build')!='90447' or
            report.get('policy')!='classic-hd-suppression-experiment-v1' or
            report.get('roundtrip_verified') is not True or report.get('scope')!='all' or
            report.get('source_overlay_kind')!='frontend' or report.get('inherited_pixel_textures')!=0 or
            report.get('classic_only') is not True or
            report.get('hd_visual_definition_references_removed') is not True or
            any(report.get(field) is not False for field in ('classic_sprites_changed',
                'ui_sprites_changed','fonts_changed','gameplay_tables_changed','original_files_modified')) or
            not isinstance(files,dict) or len(files)!=3 or 'overlay/data/.build.config' not in files or
            any(n!='overlay/data/.build.config' and not re.fullmatch(r'overlay/data/[0-9a-f]{2}-[0-9a-f]{8}\.data',n) for n in files)):
        raise ValueError('requires a verified original-size classic HD suppression overlay')
    inherited=report.get('source_frontend_files',{})
    if (not isinstance(inherited,dict) or len(inherited)!=2 or
            'overlay/data/.build.config' not in inherited or
            inherited!=report.get('source_overlay_files') or
            any(name!='overlay/data/.build.config' and files.get(name)!=proof
                for name,proof in inherited.items())):
        raise ValueError('HD suppression overlay must retain its verified frontend archive')
    manifest_path=prepared/'manifest.json'
    manifest=json.loads(manifest_path.read_text())
    if (file_sha256(manifest_path)!=report.get('source_prepared_manifest_sha256') or
            manifest.get('source_manifest_sha256')!=report.get('source_manifest_sha256')):
        raise ValueError('HD suppression overlay source identity mismatch')
    sources={row['path']:row for row in manifest['files'] if row['path'].startswith('RomFS/data/')}
    for name,expected in {**sources,**files}.items():
        root=prepared if name in sources else directory
        path=root/name
        if (path.is_symlink() or not path.resolve().is_relative_to(root.resolve()) or
                not path.is_file() or path.stat().st_size!=expected.get('size') or
                file_sha256(path)!=expected.get('sha256')):
            raise ValueError('HD suppression overlay/source checksum mismatch: '+name)
    payload={str(path.relative_to(directory)) for path in (directory/'overlay').rglob('*') if path.is_file()}
    if (payload!=set(files) or (directory/'overlay').is_symlink() or
            any(path.is_symlink() for path in (directory/'overlay').rglob('*'))):
        raise ValueError('unexpected HD suppression overlay payload')
    return directory/'overlay'


def backup_entry(entry, directory=None):
    """Retain each distinct replaced launcher without overwriting old backups."""
    directory=entry.parent if directory is None else directory
    if directory.is_symlink():
        raise ValueError('linked launcher backup directory is not supported')
    directory.mkdir(parents=True,exist_ok=True)
    data=entry.read_bytes()
    for number in range(1000):
        suffix='.before-d2r'+('' if number==0 else '.'+str(number))
        backup=directory/(entry.name+suffix)
        try:
            with backup.open('xb') as out:out.write(data)
            return backup
        except FileExistsError:
            if not backup.is_symlink() and backup.is_file() and backup.read_bytes()==data:
                return backup
    raise ValueError('too many distinct entry backups')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game-root', type=Path, required=True)
    p.add_argument('--runtime-root', type=Path, help='executable filesystem, e.g. /userdata/d2r-runtime')
    p.add_argument('--profile-root', type=Path)
    p.add_argument('--controllers', type=Path, help='PortMaster gamecontrollerdb.txt beside control.txt')
    p.add_argument('--entry', type=Path)
    p.add_argument('--classic-bundle', action='store_true', help='use prepare_classic.py resources and first-run defaults; LoongOS paths by default')
    p.add_argument('--replace-entry', action='store_true', help='retain a distinct launcher backup under the profile directory')
    modes=p.add_mutually_exclusive_group()
    modes.add_argument('--classic30',action='store_true',help='enable the tested classic quality preset; requires the two extra resource paths')
    modes.add_argument('--remaster3d',action='store_true',help='experimental 3D preset with generic runtime optimizations; no classic work cuts')
    p.add_argument('--classic-modules',type=Path,help='computer-prepared native classic format 6 directory')
    p.add_argument('--shader-image',type=Path,help='computer-optimized C1+O shaders.squashfs')
    p.add_argument('--shader-root',type=Path,help='matching shader directory/mountpoint; default: image directory/root')
    overlays=p.add_mutually_exclusive_group()
    overlays.add_argument('--weather-overlay',type=Path,help='optional complete prepare_classic_weather.py output; requires --classic30')
    overlays.add_argument('--classic-frontend',type=Path,help='optional verified classic menu overlay; scale/cache follow its policy; requires --classic30')
    overlays.add_argument('--classic-hd-suppression',type=Path,help='experimental original-size HD definition suppression pack; requires --classic30')
    overlays.add_argument('--remaster-quality',type=Path,help='optional complete prepare_remaster_quality.py output; requires --remaster3d')
    args = p.parse_args()
    if args.classic_bundle:
        if any((args.classic30,args.remaster3d,args.classic_modules,args.shader_image,args.shader_root,
                args.weather_overlay,args.classic_frontend,args.classic_hd_suppression,args.remaster_quality)):
            p.error('--classic-bundle selects its own preset/resource paths; do not combine manual presets')
    if args.classic_frontend and not args.classic30:
        p.error('--classic-frontend requires --classic30')
    if args.classic_hd_suppression and not args.classic30:
        p.error('--classic-hd-suppression requires --classic30')
    game=args.game_root.expanduser().resolve()
    manifest=verify_game(game)
    bundle=load_bundle(game,manifest) if args.classic_bundle else None
    bundle_device=bundle.get('_device') if bundle else None
    compact=bool(bundle and bundle.get('_compact'))
    if args.classic_bundle:
        defaults=install_paths(bundle_device) if bundle_device else dict(runtime_root='/userdata/d2r-runtime',profile_root='/userdata/d2r-profile',
                controllers='/mnt/sdcard/roms/ports/PortMaster/gamecontrollerdb.txt',
                entry='/mnt/sdcard/roms/ports/D2R Linux.sh')
        for name,value in defaults.items():
            if name=='game_root':continue
            if getattr(args,name) is None:setattr(args,name,Path(value))
    elif any(getattr(args,name) is None for name in ('runtime_root','profile_root','controllers','entry')):
        p.error('provide --runtime-root, --profile-root, --controllers and --entry')
    game, runtime, profile, controllers, entry = (path.expanduser().resolve() for path in
        (args.game_root, args.runtime_root, args.profile_root, args.controllers, args.entry))
    for name in ('d2r-linux-novideo', 'run-bringup.sh', 'portmaster-launch.sh.in'):
        if not (runtime / name).is_file():
            p.error('missing runtime file: ' + name)
    if not os.access(runtime / 'd2r-linux-novideo', os.X_OK):
        p.error('runtime binary is not executable; copy runtime to an executable filesystem')
    launcher_template=(runtime/'portmaster-launch.sh.in').read_text()
    if not controllers.is_file() or not (controllers.parent / 'control.txt').is_file():
        p.error('provide this firmware\'s PortMaster controller database beside control.txt')
    if args.classic_bundle:
        args.classic30=True
        args.classic_modules=bundle['modules'];args.shader_image=bundle['shaders']
        args.classic_frontend=bundle['frontend']
    patches = game / 'linux-native'
    preset_prefix=''
    if args.classic30:
        if not args.classic_modules or not args.shader_image:
            p.error('--classic30 requires --classic-modules and --shader-image')
        patches=args.classic_modules.expanduser().resolve()
        classic_report=verify_classic_modules(patches)
        if classic_report.get('game_build','90447')!=manifest['game_build'] or runtime_game_build((runtime/'d2r-linux-novideo').read_bytes())!=manifest['game_build']:
            p.error('runtime/modules/game build mismatch')
        shared_minimum=classic_shared_pool_minimum(classic_report,(runtime/'d2r-linux-novideo').read_bytes())
        if shared_minimum<384 and 'D2R_SHARED_POOL_MIN_MB' not in launcher_template:
            p.error('smaller pools require the matching launcher template; update the complete runtime')
        preset_prefix='export D2R_CLASSIC30=1\nexport D2R_REMASTER3D=0\n'
        if manifest['game_build']=='94012':
            if 'owned-content.txt' not in manifest['files'] or 'owned-content.json' not in manifest['files']:
                p.error('94012 bundle requires its verified owned-content receipt')
            preset_prefix+='export D2R_NATIVE_HD_MASK=7\nexport DM_LINUX_AOC_MANIFEST='+shlex.quote(str(game/'owned-content.txt'))+'\n'
        preset_prefix+='export D2R_SHARED_POOL_MIN_MB='+str(shared_minimum)+'\n'
    elif args.classic_modules:
        p.error('--classic-modules requires --classic30')
    if args.remaster3d:
        preset_prefix='export D2R_CLASSIC30=0\nexport D2R_REMASTER3D=1\n'
    if args.shader_image and (args.classic30 or args.remaster3d):
        image=args.shader_image.expanduser().resolve()
        shader_root=(args.shader_root.expanduser().resolve() if args.shader_root else image.parent/'root')
        if not image.is_file() or image.is_symlink() or image.stat().st_size<96:
            p.error('missing regular optimized shader image')
        with image.open('rb') as stream:
            if stream.read(4)!=b'hsqs':p.error('optimized shader image is not SquashFS')
        for path in (image,shader_root):
            if '\n' in str(path) or '\r' in str(path):p.error('resource paths must not contain newlines')
        shader_root.mkdir(parents=True,exist_ok=True)
        preset_prefix+=('export D2R_SHADER_IMAGE='+shlex.quote(str(image))+
                        '\nexport D2R_SHADER_ROOT='+shlex.quote(str(shader_root))+'\n')
        if 'D2R_PIPELINE_CACHE' not in launcher_template:
            p.error('shader bank cache isolation requires the matching launcher template')
        cache=shader_pipeline_cache_directory(image,game/'pipeline-cache')
        preset_prefix+='export D2R_PIPELINE_CACHE='+shlex.quote(str(cache))+'\n'
    elif args.shader_image or args.shader_root:
        p.error('shader resource paths require --classic30 or --remaster3d and --shader-image')
    if args.weather_overlay:
        if not args.classic30:p.error('--weather-overlay requires --classic30')
        weather=args.weather_overlay.expanduser().resolve()
        if any(c in str(weather) for c in ':\r\n'):p.error('overlay paths must not contain colons or newlines')
        extra=verify_classic_weather(weather,game/'handheld-prepared-128')
        preset_prefix+='export D2R_EXTRA_OVERLAY='+shlex.quote(str(extra))+'\n'
    if args.remaster_quality:
        if not args.remaster3d:p.error('--remaster-quality requires --remaster3d')
        quality=args.remaster_quality.expanduser().resolve()
        if any(c in str(quality) for c in ':\r\n'):p.error('overlay paths must not contain colons or newlines')
        extra=verify_remaster_quality(quality,game/'handheld-prepared-128')
        preset_prefix+='export D2R_EXTRA_OVERLAY='+shlex.quote(str(extra))+'\n'
    if args.classic_hd_suppression:
        suppression=args.classic_hd_suppression.expanduser().resolve()
        if any(c in str(suppression) for c in ':\r\n'):p.error('overlay paths must not contain colons or newlines')
        extra=verify_classic_hd_suppression(suppression,game/'handheld-prepared-128')
        preset_prefix+='export D2R_EXTRA_OVERLAY='+shlex.quote(str(extra))+'\nexport D2R_3D_SCALE=1\n'
    if compact:
        frontend=args.classic_frontend.expanduser().resolve()
        preset_prefix+='unset D2R_EXTRA_OVERLAY D2R_PREPARED_ROOT DM_LINUX_STATIC_MENU\n'
        if manifest['game_build']=='94012':
            binary=(runtime/'d2r-linux-novideo').read_bytes()
            required=(b'DM_LINUX_STATIC_MENU',b'MENU_SCENE_CACHE_PROFILE',
                      b'COUNTER_DEFERRED_PROFILE',b'LINUX_KEYBOARD_SAME_WINDOW')
            if not all(marker in binary for marker in required):
                p.error('94012 compact menus require the matching runtime with scene caching, deferred counters and keyboard support')
            preset_prefix+='export DM_LINUX_STATIC_MENU=1\n'
        preset_prefix+='export D2R_OVERLAY_ROOT='+shlex.quote(str(game/'Update/RomFS'))+'\nexport D2R_3D_SCALE=1\n'
    elif args.classic_frontend:
        frontend=args.classic_frontend.expanduser().resolve()
        if any(c in str(frontend) for c in ':\r\n'):p.error('overlay paths must not contain colons or newlines')
        extra=verify_classic_frontend(frontend,game/'handheld-prepared-128')
        policy=json.loads((frontend/'complete.json').read_text())['policy']
        character=policy.startswith('classic-frontend-character-')
        if policy in ('classic-frontend-character-v3','classic-frontend-character-v4','classic-frontend-character-v5','classic-frontend-character-v6'):
            binary=(runtime/'d2r-linux-novideo').read_bytes()
            required=(b'DM_LINUX_STATIC_MENU',b'MENU_SCENE_CACHE_PROFILE',
                      b'COUNTER_DEFERRED_PROFILE',b'LINUX_KEYBOARD_SAME_WINDOW')
            if policy in ('classic-frontend-character-v4','classic-frontend-character-v5','classic-frontend-character-v6'):
                required+=(b'DM_LINUX_INDEXED_SPRITES',b'INDEXED_SPRITE_PROFILE')
            if not all(marker in binary for marker in required):
                p.error('retained menu scenery requires the matching runtime with role caching, keyboard and indexed-sprite support')
            preset_prefix+='export DM_LINUX_STATIC_MENU=1\n'
        scale='1' if policy in ('classic-frontend-no-preview-v3','classic-frontend-character-v3','classic-frontend-character-v4','classic-frontend-character-v5','classic-frontend-character-v6') else '0.75' if character else '0.5'
        preset_prefix+='export D2R_EXTRA_OVERLAY='+shlex.quote(str(extra))+'\nexport D2R_3D_SCALE='+scale+'\n'
    if bundle_device:
        verify_components(bundle_device,classic_report,json.loads((frontend/'complete.json').read_text()))
        binary=(runtime/'d2r-linux-novideo').read_bytes()
        if ('D2R_SHARED_POOL_MAX_MB' not in launcher_template or
                not all(marker in binary for marker in (b'DM_LINUX_POOL_WINDOWS',b'DM_LINUX_COMPACT_ARRAY_IMAGES',
                        b'DM_RENDER_SLOTS',b'DM_LINUX_COMPACT_2D_IMAGES',b'DM_TEMP_CACHE_IDLE_FRAMES'))):
            p.error('device preset requires the complete current runtime and launcher template')
        for name,value in launcher_options(bundle_device).items():
            preset_prefix+='export '+name+'='+str(value)+'\n'
    for name in ('main', 'sdk', 'subsdk0'):
        for suffix in ('patched.bin', 'fixups.tsv', 'gates.tsv'):
            if not (patches / (name + '.' + suffix)).is_file():
                p.error('missing rewritten module/metadata: ' + name + '.' + suffix)
    if not compact:
        prepared = game / 'handheld-prepared-128'
        subprocess.run([sys.executable, str(prepared / 'install_handheld_pack.py'),
                        str(prepared), str(game / 'Update/RomFS')], check=True)
    text = render(launcher_template, {
        '@D2R_RUNTIME_ROOT@': runtime, '@D2R_GAME_ROOT@': game,
        '@D2R_PROFILE_ROOT@': profile, '@D2R_CONTROLLER_DB@': controllers,
        '@D2R_PATCH_ROOT@': patches})
    if preset_prefix:text=text.replace('umask 077\n','umask 077\n'+preset_prefix,1)
    if entry.exists() and entry.read_text() != text:
        if not args.replace_entry:
            p.error('entry already differs; use another --entry or --replace-entry')
    for path in (profile / 'save', profile / 'identity', entry.parent):
        path.mkdir(parents=True, exist_ok=True)
    import fcntl
    with (profile/'session.lock').open('a') as lock:
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:raise ValueError('close this profile\'s running game before installation')
        if entry.exists() and entry.read_text()!=text:
            if not args.replace_entry:raise ValueError('entry changed before installation; use --replace-entry')
            backup_entry(entry,profile/'launcher-backups')
        settings_created=seed_settings(profile,json.loads(bundle['settings'].read_text())) if args.classic_bundle else False
        temporary = entry.with_name(entry.name + '.new')
        with temporary.open('x', encoding='utf-8') as out:
            out.write(text)
        temporary.chmod(0o755)
        temporary.replace(entry)
    print(json.dumps(dict(installed=True, entry=str(entry), profile=str(profile),
        game_started=False, existing_saves_edited=False, settings_initialized=settings_created,
        classic_bundle=args.classic_bundle,next='launch from the device menu'), ensure_ascii=False))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit('install: ' + str(error))
