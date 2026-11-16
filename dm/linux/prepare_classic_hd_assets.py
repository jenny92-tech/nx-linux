#!/usr/bin/env python3
# INPUT: User-owned 90447 RomFS, verified prepared pack and frontend overlay.
# OUTPUT: Separate 1x1 HD-world texture experiment; classic sprites/UI are retained.
# POS: Offline resource experiment; never changes source assets or installs itself.
"""Replace supported HD world textures with valid 1x1 ASTC placeholders.

This is for classic-mode experiments, not a remastered-quality preset. It also
disables the selected-character/menu cameras. Fonts, UI sprites, classic art,
gameplay tables, models and skeletons retain their original payloads. The
receipt reports exactly what changed; smaller files are not a RAM measurement.
The all-1x1 90447 candidate failed the fixed-atlas copy extent check on device.
Use prepare_classic_hd_suppression.py for the tested original-size approach.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import shutil
import sys

from handheld_mips import NativeLayout, FORMATS, logical_path, pixel_texture, world_texture
from install_handheld_pack import install, safe_path, sha256
from install_game import verify_classic_frontend
from prepare_classic_frontend import CACHE, LAYOUT, disable_cameras, overlay_asset
from prepare_remaster_quality import strict_json
import casc_overlay as casc


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False) + '\n')


def supported(entry):
    return (world_texture(entry['filename']) and entry['format'] in FORMATS and
            entry['type'] == 2 and entry['depth'] == 1 and entry['layoutType'] == 3)


def verify_frontend(frontend, base, prepared):
    raw = (frontend / 'complete.json').read_bytes()
    report = strict_json(raw)
    verify_classic_frontend(frontend, prepared)
    if (not report.get('complete') or report.get('game_build') != '90447' or
            not report.get('roundtrip_verified') or
            report.get('source_manifest_sha256') != sha256(base / 'data/build_manifest.psv') or
            report.get('source_prepared_manifest_sha256') != sha256(prepared / 'manifest.json')):
        raise ValueError('frontend must match this verified original/prepared resource pack')
    for name, info in report['output_files'].items():
        path = safe_path(frontend, name)
        if (not name.startswith('overlay/data/') or path.is_symlink() or
                not path.is_file() or path.stat().st_size != info['size'] or
                sha256(path) != info['sha256']):
            raise ValueError('frontend payload mismatch: ' + name)
    actual = {str(p.relative_to(frontend)) for p in (frontend / 'overlay').rglob('*') if p.is_file()}
    if actual != set(report['output_files']):
        raise ValueError('unexpected frontend overlay payload')
    return raw, report


def prepare(base, prepared, frontend, out, compiler='cc'):
    base, prepared, frontend, out = (p.expanduser().resolve() for p in (base, prepared, frontend, out))
    if out.exists() or any(p == out or p in out.parents or out in p.parents for p in (base, prepared, frontend)):
        raise ValueError('choose a new, separate private output directory')
    install(prepared, base, check_only=True)
    frontend_raw, frontend_proof = verify_frontend(frontend, base, prepared)
    prepared_hash = sha256(prepared / 'manifest.json')
    out.mkdir(parents=True, mode=0o700)
    source = out / 'work/source'
    (source / 'data').mkdir(parents=True)
    for directory in (base / 'data', prepared / 'RomFS/data', frontend / 'overlay/data'):
        for path in directory.iterdir():
            if path.is_file():
                target = source / 'data' / path.name
                if target.is_symlink():
                    target.unlink()
                target.symlink_to(path)
    manifest = casc.read_manifest(source)
    tvfs = casc.load_vfs1(source)
    original_cache = overlay_asset(source, manifest, tvfs, CACHE)
    entries = strict_json(original_cache)
    names = [logical_path(e['filename']) for e in entries]
    if len(names) != len(set(names)):
        raise ValueError('duplicate texture descriptor')
    native = NativeLayout(out / 'work/native', compiler)
    replacements, changed, skipped = {}, [], Counter()
    source_bytes = output_bytes = 0
    files = out / 'work/files'
    files.mkdir()
    for index, (entry, name) in enumerate(zip(entries, names)):
        if not world_texture(name):
            skipped['outside_hd_world_texture_paths'] += 1
            continue
        if not supported(entry):
            skipped['unsupported_format_or_geometry'] += 1
            continue
        if (entry['width'], entry['height'], entry['levels']) == (1, 1, 1):
            skipped['already_1x1'] += 1
            continue
        raw = overlay_asset(source, manifest, tvfs, name)
        result, updated = pixel_texture(raw, entry, native)
        target = files / ('texture-%05d.bin' % index)
        target.write_bytes(result)
        replacements[name] = target
        entries[index] = updated
        source_bytes += len(raw)
        output_bytes += len(result)
        changed.append(dict(path=name, source_sha256=hashlib.sha256(raw).hexdigest(),
            output_sha256=hashlib.sha256(result).hexdigest(),
            before=[entry['width'], entry['height'], entry['levels']], after=[1, 1, 1],
            source_bytes=len(raw), output_bytes=len(result)))
        if len(changed) % 2000 == 0:
            print('1x1 HD textures:', len(changed), flush=True)
    if not changed:
        raise ValueError('no supported HD textures changed')
    cache = files / 'texture_desc_cache.json'
    cache.write_text(json.dumps(entries, ensure_ascii=False, separators=(',', ':')) + '\n')
    replacements[CACHE] = cache
    layout = files / 'mainmenupanelhd.json'
    original_layout = overlay_asset(source, manifest, tvfs, LAYOUT)
    layout.write_bytes(disable_cameras(original_layout, True))
    replacements[LAYOUT] = layout
    rebuilt = out / 'work/rebuilt'
    casc.make_overlay(source, rebuilt, replacements)
    new_tvfs = casc.load_vfs1(rebuilt)
    for name, path in replacements.items():
        if overlay_asset(rebuilt, manifest, new_tvfs, name) != path.read_bytes():
            raise ValueError('CASC round-trip mismatch: ' + name)
    # This overlay is above the prepared pack. Carry forward the frontend's
    # data archive, since its unchanged entries are still named by the TVFS.
    destination = out / 'overlay/data'
    destination.mkdir(parents=True)
    for directory in (frontend / 'overlay/data', rebuilt / 'data'):
        for path in directory.iterdir():
            if path.is_file() and not path.is_symlink():
                shutil.copyfile(path, destination / path.name)
    output_files = {'overlay/data/' + p.name: dict(size=p.stat().st_size, sha256=sha256(p))
                    for p in destination.iterdir()}
    if ((frontend / 'complete.json').read_bytes() != frontend_raw or
            sha256(prepared / 'manifest.json') != prepared_hash):
        raise ValueError('source metadata changed during preparation')
    report = dict(schema=1, complete=True, game_build='90447',
        policy='classic-hd-1x1-experiment-v1', classic_only=True,
        source_manifest_sha256=sha256(base / 'data/build_manifest.psv'),
        source_prepared_manifest_sha256=prepared_hash,
        source_frontend_sha256=hashlib.sha256(frontend_raw).hexdigest(),
        source_frontend_files=frontend_proof['output_files'],
        source_cache_sha256=hashlib.sha256(original_cache).hexdigest(),
        source_layout_sha256=hashlib.sha256(original_layout).hexdigest(),
        texture_count=len(entries), changed_textures=len(changed), skipped=dict(skipped),
        changed_source_bytes=source_bytes, changed_output_bytes=output_bytes,
        menu_preview_disabled=True, classic_sprites_changed=False, ui_sprites_changed=False,
        fonts_changed=False, gameplay_tables_changed=False, models_or_skeletons_changed=False,
        load_requests_disabled=False, native_layout_verified=True, roundtrip_verified=True,
        original_files_modified=False, runtime_rendering_verified=False,
        memory_saving_verified=False, output_files=output_files, textures=changed,
        tool_sha256=sha256(Path(__file__)))
    write_json(out / 'complete.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('romfs', 'prepared', 'frontend', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    result = prepare(args.romfs, args.prepared, args.frontend, args.out, args.cc)
    print(json.dumps({key: result[key] for key in ('complete', 'policy', 'changed_textures',
        'changed_source_bytes', 'changed_output_bytes', 'skipped', 'runtime_rendering_verified')}))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError) as error:
        sys.exit('classic HD resource preparation: ' + str(error))
