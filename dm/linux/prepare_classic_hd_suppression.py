#!/usr/bin/env python3
# INPUT: User-owned 90447 RomFS, verified handheld pack and frontend/pixel overlay.
# OUTPUT: Isolated experimental definitions without HD visual dependencies.
# POS: Offline classic-only experiment; original assets and saves remain intact.
"""Remove HD unit and scene resource references in a separate test overlay.

Accepts a verified frontend or prepare_classic_hd_assets.py output. Unit roots and scene
terrain identities remain valid JSON objects; models, skeletons, animations,
particles and their preload lists are omitted. This is an experimental asset
contract change, not proof that the game accepts it or that RAM usage improves.
Classic sprites, UI sprite payloads, fonts, audio and gameplay tables are retained.
The optional flat menu removes only its reviewed 3D widgets from the UI layout.
"""
import argparse
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

from install_handheld_pack import install, safe_path, sha256
from prepare_classic_frontend import LAYOUT, overlay_asset
from prepare_classic_hd_assets import write_json, verify_frontend
from prepare_remaster_quality import strict_json
import casc_overlay as casc

POLICY = 'classic-hd-suppression-experiment-v1'
UNIT_TYPES = {'UnitDefinition', 'OverlayDefinition', 'UnitDefinitionPreloadFile'}
SCENE_TYPES = {'Preset', 'Prefab'}
PREFIXES = ('data/hd/character/', 'data/hd/items/', 'data/hd/objects/',
            'data/hd/missiles/', 'data/hd/roomtiles/', 'data/hd/overlays/',
            'data/hd/env/', 'data/hd/vfx/', 'data/hd/global/act_')


def suppress_menu_loaders(raw):
    """Remove the reviewed 3D widgets, retaining the complete 2D control tree."""
    # Blizzard layouts permit comments and trailing commas. Tokenize strings
    # first so comment markers and punctuation inside UI strings stay intact.
    strings = r'"(?:\\.|[^"\\])*"'
    text = re.sub(strings + r'|//[^\r\n]*|/\*[\s\S]*?\*/',
                  lambda match: match[0] if match[0].startswith('"') else ' ',
                  raw.decode('utf-8'))
    text = re.sub(strings + r'|,\s*(?=[}\]])',
                  lambda match: match[0] if match[0].startswith('"') else '', text)
    data = strict_json(text)
    if (not isinstance(data, dict) or data.get('type') != 'MainMenuHDPanel' or
            data.get('name') != 'MainMenuPanel' or not isinstance(data.get('fields'), dict) or
            not isinstance(data.get('children'), list)):
        raise ValueError('reviewed main menu layout required')
    expected = {('Camera3DWidget', 'Act%dCamera' % i) for i in range(1, 6)}
    expected |= {('PlayerMoverWidget', 'Act%dPlayer' % i) for i in range(1, 6)}
    expected |= {('PlayerLoaderWidget', 'Preload' + name) for name in
                 ('Amazon', 'Sorceress', 'Necromancer', 'Paladin', 'Barbarian', 'Druid', 'Assassin')}
    expected.add(('PlayerLoaderWidget', 'Player'))
    controls = {('ButtonWidget', 'QuickPlayButton'), ('ImageWidget', 'Divider'),
                ('ButtonWidget', 'SecondaryContextButton'), ('RectangleWidget', 'PlayerInfoContainer')}
    found, kept, removed = set(), [], []
    for child in data['children']:
        if not isinstance(child, dict):
            raise ValueError('reviewed main menu child required')
        identity = child.get('type'), child.get('name')
        if identity in found or identity not in expected | controls:
            raise ValueError('unexpected/duplicate main menu child: ' + repr(identity))
        found.add(identity)
        if identity in expected:
            removed.append(dict(type=identity[0], name=identity[1]))
        else:
            kept.append(child)
    if found != expected | controls:
        raise ValueError('complete reviewed 18 visual widgets and 4 UI controls required')
    data['children'] = kept
    return (json.dumps(data, ensure_ascii=False, separators=(',', ':')) + '\n').encode(), removed


def empty_dependencies(data):
    dependencies = data.get('dependencies')
    if not isinstance(dependencies, dict) or any(not isinstance(v, list) for v in dependencies.values()):
        raise ValueError('reviewed dependency-list dictionary required')
    data['dependencies'] = {key: [] for key in dependencies}


def entities(data):
    rows = data.get('entities')
    if not isinstance(rows, list):
        raise ValueError('entity list required')
    for row in rows:
        if (not isinstance(row, dict) or row.get('type') != 'Entity' or
                not isinstance(row.get('components'), list) or
                any(not isinstance(c, dict) or not isinstance(c.get('type'), str)
                    for c in row['components'])):
            raise ValueError('reviewed entity/component schema required')
    return rows


def suppress_definition(original):
    """Return a typed empty visual definition, rejecting unreviewed schemas."""
    result = copy.deepcopy(original)
    kind = result.get('type')
    if kind not in UNIT_TYPES | SCENE_TYPES:
        raise ValueError('unsupported visual definition type')
    empty_dependencies(result)
    if kind == 'UnitDefinitionPreloadFile':
        for name in ('players', 'monsters', 'objects', 'missiles', 'items', 'roomtiles'):
            if not isinstance(result.get(name), list):
                raise ValueError('reviewed act preload list required')
            result[name] = []
    elif kind == 'UnitDefinition':
        roots = [(row, component) for row in entities(result) for component in row['components']
                 if component['type'] == 'UnitRootComponent']
        if len(roots) != 1:
            raise ValueError('one unit root required')
        row, root = roots[0]
        for name in ('state_machine_filename', 'onCreateEventName'):
            if not isinstance(root.get(name), str):
                raise ValueError('reviewed unit root strings required')
            root[name] = ''
        if not isinstance(root.get('animations'), list):
            raise ValueError('reviewed unit root animations required')
        root['animations'] = []
        row['components'] = [c for c in row['components']
                             if c is root or c['type'] == 'TransformDefinitionComponent']
        result['entities'] = [row]
    elif kind in ('OverlayDefinition', 'Prefab'):
        rows = entities(result)
        # Authored empty overlays already use an identity-only root entity.
        result['entities'] = [dict(rows[0], components=[])] if rows else []
    else:
        entities(result)
        result['entities'] = []
        if 'terrain' in result:
            terrain = result['terrain']
            if terrain not in (None, {}):
                entities({'entities': [terrain]})
                terrain['components'] = [c for c in terrain['components'] if c['type'] in
                                         ('TransformDefinitionComponent', 'TerrainDefinitionComponent')]
        if 'biomeFilename' in result:
            if not isinstance(result['biomeFilename'], str):
                raise ValueError('reviewed biome filename required')
            result['biomeFilename'] = ''
        if 'perTileBiomeOverrides' in result:
            if not isinstance(result['perTileBiomeOverrides'], list):
                raise ValueError('reviewed tile biome overrides required')
            result['perTileBiomeOverrides'] = []
        if 'specialTiles' in result:
            if not isinstance(result['specialTiles'], dict):
                raise ValueError('reviewed HD special-tile mapping required')
            result['specialTiles'] = {}
    # A residual path would silently reintroduce loading. Fail instead of
    # editing unknown fields or returning a partially suppressed definition.
    def check(value):
        if isinstance(value, dict):
            for item in value.values():
                check(item)
        elif isinstance(value, list):
            for item in value:
                check(item)
        elif isinstance(value, str) and ('data/hd/' in value.lower().replace('\\', '/') or
                                         value.lower().endswith(('.model', '.skeleton', '.animations', '.particles', '.texture', '.physics'))):
            raise ValueError('unreviewed residual visual resource reference: ' + value)
    check(result)
    return result


def verify_pixel(pixel, base, prepared):
    raw = (pixel / 'complete.json').read_bytes()
    report = strict_json(raw)
    if (report.get('policy') != 'classic-hd-1x1-experiment-v1' or
            report.get('complete') is not True or report.get('roundtrip_verified') is not True or
            report.get('game_build') != '90447' or
            report.get('source_manifest_sha256') != sha256(base / 'data/build_manifest.psv') or
            report.get('source_prepared_manifest_sha256') != sha256(prepared / 'manifest.json')):
        raise ValueError('verified pixel overlay for the exact original/prepared pack required')
    for name, proof in report['output_files'].items():
        path = safe_path(pixel, name)
        if (not re.fullmatch(r'overlay/data/(?:\.build\.config|[0-9a-f]{2}-[0-9a-f]{8}\.data)', name) or
                path.is_symlink() or not path.is_file() or path.stat().st_size != proof['size'] or
                sha256(path) != proof['sha256']):
            raise ValueError('pixel overlay payload mismatch: ' + name)
    if {str(p.relative_to(pixel)) for p in (pixel / 'overlay').rglob('*') if p.is_file()} != set(report['output_files']):
        raise ValueError('unexpected pixel overlay payload')
    return raw, report


def prepare(base, prepared, pixel, out, scope='all', from_frontend=False, flat_menu=False):
    if scope not in ('units', 'scenes', 'all'):
        raise ValueError('scope must be units, scenes or all')
    base, prepared, pixel, out = (p.expanduser().resolve() for p in (base, prepared, pixel, out))
    if out.exists() or any(p == out or p in out.parents or out in p.parents for p in (base, prepared, pixel)):
        raise ValueError('choose a new, separate private output directory')
    install(prepared, base, check_only=True)
    raw, prior = (verify_frontend(pixel, base, prepared) if from_frontend else
                  verify_pixel(pixel, base, prepared))
    if from_frontend:
        prior = dict(prior, source_frontend_files=prior['output_files'])
    out.mkdir(parents=True, mode=0o700)
    source = out / 'work/source'
    (source / 'data').mkdir(parents=True)
    for directory in (base / 'data', prepared / 'RomFS/data', pixel / 'overlay/data'):
        for path in directory.iterdir():
            if path.is_file():
                target = source / 'data' / path.name
                if target.is_symlink():
                    target.unlink()
                target.symlink_to(path)
    manifest = casc.read_manifest(source)
    tvfs = casc.load_vfs1(source)
    files = out / 'work/files'
    files.mkdir()
    replacements, changes, counts = {}, [], Counter()
    allowed = UNIT_TYPES if scope == 'units' else SCENE_TYPES if scope == 'scenes' else UNIT_TYPES | SCENE_TYPES
    for name in manifest:
        if not name.startswith(PREFIXES) or not name.endswith('.json'):
            continue
        before = overlay_asset(source, manifest, tvfs, name)
        data = strict_json(before)
        if not isinstance(data, dict) or data.get('type') not in allowed:
            continue
        try:
            updated = suppress_definition(data)
        except ValueError as error:
            raise ValueError(name + ': ' + str(error)) from error
        after = (json.dumps(updated, ensure_ascii=False, separators=(',', ':')) + '\n').encode()
        target = files / ('definition-%05d.json' % len(changes))
        target.write_bytes(after)
        replacements[name] = target
        counts[data['type']] += 1
        changes.append(dict(path=name, type=data['type'], source_sha256=hashlib.sha256(before).hexdigest(),
                            output_sha256=hashlib.sha256(after).hexdigest(),
                            source_bytes=len(before), output_bytes=len(after),
                            removed_dependencies={k: len(v) for k, v in data['dependencies'].items()}))
    if not changes:
        raise ValueError('no reviewed visual definitions found')
    menu_change = None
    if flat_menu:
        before = overlay_asset(source, manifest, tvfs, LAYOUT)
        after, removed = suppress_menu_loaders(before)
        target = files / 'mainmenupanelhd.json'
        target.write_bytes(after)
        replacements[LAYOUT] = target
        menu_change = dict(path=LAYOUT, removed_widgets=removed,
                           source_sha256=hashlib.sha256(before).hexdigest(),
                           output_sha256=hashlib.sha256(after).hexdigest(),
                           ui_control_tree_preserved=True)
    rebuilt = out / 'work/rebuilt'
    casc.make_overlay(source, rebuilt, replacements)
    new_tvfs = casc.load_vfs1(rebuilt)
    for name, path in replacements.items():
        if overlay_asset(rebuilt, manifest, new_tvfs, name) != path.read_bytes():
            raise ValueError('CASC round-trip mismatch: ' + name)
    destination = out / 'overlay/data'
    destination.mkdir(parents=True)
    for directory in (pixel / 'overlay/data', rebuilt / 'data'):
        for path in directory.iterdir():
            if path.is_file() and not path.is_symlink():
                shutil.copyfile(path, destination / path.name)
    if (pixel / 'complete.json').read_bytes() != raw:
        raise ValueError('source pixel metadata changed during preparation')
    output_files = {'overlay/data/' + p.name: dict(size=p.stat().st_size, sha256=sha256(p))
                    for p in destination.iterdir()}
    report = dict(schema=1, complete=True, game_build='90447', policy=POLICY, scope=scope,
        source_manifest_sha256=prior['source_manifest_sha256'],
        source_prepared_manifest_sha256=prior['source_prepared_manifest_sha256'],
        source_frontend_files=prior['source_frontend_files'],
        source_overlay_kind='frontend' if from_frontend else 'pixel',
        source_overlay_receipt_sha256=hashlib.sha256(raw).hexdigest(),
        source_overlay_files=prior['output_files'],
        changed_definitions=len(changes), definition_types=dict(counts), definitions=changes,
        inherited_pixel_textures=prior.get('changed_textures', 0),
        main_menu_3d_widgets_removed=flat_menu, main_menu_change=menu_change,
        classic_only=True, classic_sprites_changed=False, ui_sprites_changed=False,
        fonts_changed=False, gameplay_tables_changed=False, original_files_modified=False,
        hd_visual_definition_references_removed=True, all_hd_loads_disabled_verified=False,
        roundtrip_verified=True, runtime_rendering_verified=False, memory_saving_verified=False,
        output_files=output_files, tool_sha256=sha256(Path(__file__)))
    write_json(out / 'complete.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('romfs', 'prepared', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--pixel', type=Path)
    source.add_argument('--frontend', type=Path)
    parser.add_argument('--scope', choices=('units', 'scenes', 'all'), default='all')
    parser.add_argument('--flat-menu', action='store_true',
                        help='also omit the reviewed main-menu cameras and player loaders')
    args = parser.parse_args()
    result = prepare(args.romfs, args.prepared, args.pixel or args.frontend, args.out,
                     args.scope, from_frontend=args.frontend is not None, flat_menu=args.flat_menu)
    print(json.dumps({key: result[key] for key in ('complete', 'policy', 'scope',
                      'changed_definitions', 'definition_types', 'runtime_rendering_verified')}))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError) as error:
        sys.exit('classic HD suppression: ' + str(error))
