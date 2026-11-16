#!/usr/bin/env python3
# INPUT: User-owned 90447 RomFS, verified handheld pack and explicit visual cuts.
# OUTPUT: Separate hash-verified CASC overlay; source assets stay unchanged.
# POS: Computer-only 3D quality preparation; no gameplay tables or runtime patches.
"""Reduce authored decoration, sun shadows or bloom before installing the game.

Only terrain-layer GrassDensity/ClutterDensity and reviewed BiomeVisualData
component fields are changed. Keep the original terrain, actors, lights and
render dimensions. Actual rendering and performance require device validation.
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import struct
import sys
import zlib

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'tools'))
import casc_overlay as casc
from install_handheld_pack import install, sha256

TIERS = ('terrainDataLow', 'terrainDataMed', 'terrainDataHigh', 'terrainDataUltra')


def strict_json(raw):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('duplicate asset JSON key: ' + key)
            result[key] = value
        return result
    def nonfinite(value):
        raise ValueError('nonfinite JSON: ' + value)
    return json.loads(raw, object_pairs_hook=unique, parse_constant=nonfinite)


def cut_biome(original, scale):
    """Scale only population densities in the four authored terrain tiers."""
    if (not isinstance(original, dict) or original.get('type') != 'Biome' or
        type(scale) not in (int, float) or not math.isfinite(scale) or not 0 <= scale <= 1):
        raise ValueError('biome object and finite density scale 0..1 required')
    result, changes = copy.deepcopy(original), []
    for tier in TIERS:
        if tier not in result:
            continue
        data = result[tier]
        if not isinstance(data, dict) or not isinstance(data.get('terrainLayers'), list):
            raise ValueError('unreviewed terrain layer schema: ' + tier)
        for i, layer in enumerate(data['terrainLayers']):
            if not isinstance(layer, dict):
                raise ValueError('terrain layer must be an object')
            for field in ('ClutterDensity', 'GrassDensity'):
                if field not in layer:
                    continue
                before = layer[field]
                if type(before) not in (int, float) or not math.isfinite(before) or before < 0:
                    raise ValueError('invalid population density: ' + field)
                after = before * scale
                if after != before:
                    layer[field] = after
                    changes.append(dict(path=f'/{tier}/terrainLayers/{i}/{field}', before=before, after=after))
    return result, changes


def cut_visual(original, shadows, bloom):
    """Leave lights in place; remove their shadow request and/or bloom gain."""
    if (not isinstance(original, dict) or original.get('type') != 'VisualDataDefinition' or
        not isinstance(original.get('entities'), list)):
        raise ValueError('unreviewed biome visual schema')
    result, changes = copy.deepcopy(original), []
    for e, entity in enumerate(result['entities']):
        if not isinstance(entity, dict) or not isinstance(entity.get('components'), list):
            raise ValueError('unreviewed visual entity schema')
        for i, component in enumerate(entity['components']):
            if not isinstance(component, dict):
                raise ValueError('visual component must be an object')
            for field, enabled, after in (('shadowCaster', shadows, False), ('bloomIntensity', bloom, 0.0)):
                if not enabled or field not in component:
                    continue
                expected = 'DirectionalLightDefinitionComponent' if field == 'shadowCaster' else 'VisualDataComponent'
                if component.get('type') != expected:
                    continue
                before = component[field]
                if (field == 'shadowCaster' and type(before) is not bool or
                    field == 'bloomIntensity' and (type(before) not in (int, float) or not math.isfinite(before) or before < 0)):
                    raise ValueError('unreviewed visual parameter type: ' + field)
                if before != after:
                    component[field] = after
                    changes.append(dict(path=f'/entities/{e}/components/{i}/{field}', before=before, after=after))
    return result, changes


def read_asset(base, manifest, name):
    row = manifest[name]
    key = bytes.fromhex(row[6])
    with (base / 'data' / casc.archive_filename(key[9],key[10])).open('rb') as stream:
        stream.seek(int.from_bytes(key[11:16], 'big'))
        encoded = stream.read(int(row[7]))
    if len(encoded) != int(row[7]):
        raise ValueError('truncated source asset: ' + name)
    raw = zlib.decompress(encoded) if row[8] == 'z' else encoded if row[8] == 'n' else casc.read_data_file(base / 'data', key, int(row[7]))
    if len(raw) != int(row[5]) or hashlib.md5(raw).hexdigest() != row[4]:
        raise ValueError('source CASC identity mismatch: ' + name)
    return raw


def prepare(base, prepared, out, biomes, scale, shadows, bloom):
    base, prepared, out = base.resolve(), prepared.resolve(), out.resolve()
    if out.exists() or any(p == out or p in out.parents or out in p.parents for p in (base, prepared)):
        raise ValueError('choose a new, separate private output directory')
    if scale is None and not (shadows or bloom):
        raise ValueError('select at least one explicit visual cut')
    install(prepared, base, check_only=True)
    prepared_raw = (prepared / 'manifest.json').read_bytes()
    proof = strict_json(prepared_raw)
    manifest = casc.read_manifest(base)
    names = sorted(n for n in manifest if n.startswith('data/hd/env/biome/') and n.endswith('.json'))
    if biomes:
        names = ['data/hd/env/biome/' + n + '.json' for n in sorted(set(biomes))]
        if any(n not in manifest for n in names):
            raise ValueError('requested biome absent from this game')
    if not names:
        raise ValueError('no authored biomes found')
    payloads, records, visual_names = {}, [], set()

    def remember(name, raw, changed, changes):
        if not changes:
            return
        encoded = (json.dumps(changed, ensure_ascii=False, indent=2, allow_nan=False) + '\n').encode()
        assert strict_json(encoded) == changed
        payloads[name] = encoded
        records.append(dict(path=name, source_sha256=hashlib.sha256(raw).hexdigest(),
                            output_sha256=hashlib.sha256(encoded).hexdigest(), changes=changes))

    for name in names:
        raw = read_asset(base, manifest, name)
        original = strict_json(raw)
        if scale is not None:
            changed, changes = cut_biome(original, scale)
            remember(name, raw, changed, changes)
        if shadows or bloom:
            for item in original.get('visualDataFilenames', []):
                n = item['visualDataFilename'].lower()
                if not re.fullmatch(r'data/hd/env/vis/[a-z0-9_/-]+\.json', n) or n not in manifest:
                    raise ValueError('unreviewed visual asset reference')
                visual_names.add(n)
    for name in sorted(visual_names):
        raw = read_asset(base, manifest, name)
        changed, changes = cut_visual(strict_json(raw), shadows, bloom)
        remember(name, raw, changed, changes)
    if not payloads:
        raise ValueError('selected cuts change no authored resources')
    out.mkdir(parents=True, mode=0o700)
    source = out / 'work/source'
    (source / 'data').mkdir(parents=True)
    for directory in (base / 'data', prepared / 'RomFS/data'):
        for path in directory.iterdir():
            if not path.is_file():
                continue
            target = source / 'data' / path.name
            if target.is_symlink():
                target.unlink()
            target.symlink_to(path)
    tvfs = casc.load_vfs1(source)
    offset, size = struct.unpack_from('>II', tvfs, 28)
    positions = {n: casc.cft_position(tvfs, offset, offset + size, manifest, n) for n in payloads}
    replacements = {}
    for i, (name, raw) in enumerate(payloads.items()):
        p = out / 'work' / f'asset-{i}.json'
        p.write_bytes(raw)
        replacements[name] = p
    rebuilt = out / 'work/rebuilt'
    casc.make_overlay(source, rebuilt, replacements)
    tvfs = casc.load_vfs1(rebuilt)
    for name, position in positions.items():
        key = bytes(tvfs[position:position + 16])
        size = struct.unpack_from('>I', tvfs, position + 16)[0]
        with (rebuilt / 'data' / casc.archive_filename(key[9],key[10])).open('rb') as stream:
            start = int.from_bytes(key[11:16], 'big')
            stream.seek(start)
            encoded = stream.read(size)
        if casc.ekey_for(encoded, key[9], key[10], start) != key or zlib.decompress(encoded) != payloads[name]:
            raise ValueError('generated overlay round-trip mismatch: ' + name)
    target = out / 'overlay/data'
    target.mkdir(parents=True)
    for path in (rebuilt / 'data').iterdir():
        if path.is_file() and not path.is_symlink():
            shutil.copyfile(path, target / path.name)
    files = {'overlay/data/' + p.name: dict(size=p.stat().st_size, sha256=sha256(p)) for p in target.iterdir()}
    if len(files) != 2 or 'overlay/data/.build.config' not in files:
        raise ValueError('unexpected overlay file layout')
    if (prepared / 'manifest.json').read_bytes() != prepared_raw:
        raise ValueError('source prepared manifest changed')
    result = dict(schema=1, game_build='90447', complete=True, policy='remaster-authored-visual-cuts-v1',
        clutter_scale=scale, sun_shadows_disabled=shadows, bloom_disabled=bloom,
        selected_biomes=names, assets=records, output_files=files,
        source_manifest_sha256=proof['source_manifest_sha256'],
        source_prepared_manifest_sha256=hashlib.sha256(prepared_raw).hexdigest(),
        tool_sha256=sha256(Path(__file__)), roundtrip_verified=True,
        render_dimensions_changed=False, original_files_modified=False,
        runtime_rendering_verified=False, target_fps_verified=False)
    (out / 'complete.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--romfs', type=Path, required=True)
    p.add_argument('--prepared', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--biome', action='append', help='optional base name, e.g. act1_outdoors; default all biomes')
    p.add_argument('--clutter-scale', type=float, help='decorative grass/clutter population multiplier 0..1')
    p.add_argument('--disable-sun-shadows', action='store_true')
    p.add_argument('--disable-bloom', action='store_true')
    a = p.parse_args()
    if a.biome and any(not re.fullmatch(r'[a-z0-9_]+', n) for n in a.biome):
        p.error('biome names require lowercase letters, digits and underscores')
    if a.clutter_scale is not None and (not math.isfinite(a.clutter_scale) or not 0 <= a.clutter_scale <= 1):
        p.error('clutter scale requires a finite number 0..1')
    result = prepare(a.romfs, a.prepared, a.out, a.biome, a.clutter_scale,
                     a.disable_sun_shadows, a.disable_bloom)
    print(json.dumps(dict(complete=True, assets=len(result['assets']), output=str(a.out),
        copy_only=['overlay/', 'complete.json'], runtime_rendering_verified=False)))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError, struct.error) as error:
        sys.exit('remaster quality preparation: ' + str(error))
