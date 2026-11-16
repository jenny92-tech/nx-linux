#!/usr/bin/env python3
# INPUT: Prepared ASTC4 work/repl.tsv, native NVN layout code and local astcenc.
# OUTPUT: Separate ASTC8 work tree, matching thumbnails and per-texture proofs.
# POS: Host-only threshold conversion; dimensions, mip count and sources survive.
"""Recompress every supported ASTC4 texture above a source-edge threshold."""
import argparse
import base64
import errno
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
import time
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image

from handheld_mips import NativeLayout, descriptor, logical_path, read_texture, validate_thumbnail

SOURCE_FORMATS = {95: 102, 81: 88}
CACHE_PATH = 'data/hd/texture_desc_cache.json'
ASTC_MAGIC = bytes.fromhex('13aba15c')
ATLAS_COLUMNS = 512


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def atlas_geometry(count, block):
    if count <= 0:
        raise ValueError('empty ASTC block atlas')
    columns = min(ATLAS_COLUMNS, count)
    rows = (count + columns - 1) // columns
    return columns, rows, columns * block, rows * block


def astc_file(blocks, block):
    if not blocks or len(blocks) % 16:
        raise ValueError('ASTC input is not complete blocks')
    count = len(blocks) // 16
    columns, rows, width, height = atlas_geometry(count, block)
    header = ASTC_MAGIC + bytes((block, block, 1))
    header += width.to_bytes(3, 'little') + height.to_bytes(3, 'little') + bytes((1, 0, 0))
    return header + blocks + bytes((columns * rows - count) * 16)


def pixels_to_blocks(image, block, count):
    height, width, channels = image.shape
    if channels != 4 or width % block or height % block:
        raise ValueError('invalid decoded ASTC atlas geometry')
    return image.reshape(height // block, block, width // block, block, 4).transpose(
        0, 2, 1, 3, 4).reshape(-1, block, block, 4)[:count]


def encode_blocks(pixels, block, srgb, astcenc, preset, temporary):
    """Encode independent RGBA blocks in one codec process, without channel edits."""
    count = len(pixels)
    columns, rows, width, height = atlas_geometry(count, block)
    padded = np.zeros((columns * rows, block, block, 4), dtype=np.uint8)
    padded[:count] = pixels
    image = padded.reshape(rows, columns, block, block, 4).transpose(0, 2, 1, 3, 4).reshape(height, width, 4)
    source, target = Path(temporary) / 'encode.tga', Path(temporary) / 'encode.astc'
    Image.fromarray(image).save(source)
    subprocess.run([str(astcenc), '-cs' if srgb else '-cl', str(source), str(target),
                    f'{block}x{block}', '-' + preset, '-silent'], check=True)
    raw = target.read_bytes()
    expected = astc_file(bytes(count * 16), block)[:16]
    if raw[:16] != expected or len(raw) != 16 + columns * rows * 16:
        raise ValueError('astcenc output header/size differs from atlas')
    return raw[16:16 + count * 16]


def transcode_batch(chains, srgb, astcenc, preset, temporary):
    """ASTC4 chains [(width,height,linear blocks)] → matching ASTC8 chains.

    The decode atlas preserves each independent 4x4 block. Mips are reconstructed
    spatially before collecting 8x8 blocks, so adjacent source blocks combine
    within their own mip. Tiny mips repeat their edge texels to fill one block.
    """
    source_blocks = b''.join(data for chain in chains for _, _, data in chain)
    count = len(source_blocks) // 16
    source, decoded = Path(temporary) / 'decode.astc', Path(temporary) / 'decode.tga'
    source.write_bytes(astc_file(source_blocks, 4))
    subprocess.run([str(astcenc), '-ds' if srgb else '-dl', str(source), str(decoded), '-silent'], check=True)
    with Image.open(decoded) as image:
        rgba = np.array(image.convert('RGBA'))
    _, _, width, height = atlas_geometry(count, 4)
    if rgba.shape != (height, width, 4):
        raise ValueError('astcenc decoded atlas dimensions changed')
    blocks4 = pixels_to_blocks(rgba, 4, count)
    target_count = sum(((w + 7) // 8) * ((h + 7) // 8) for chain in chains for w, h, _ in chain)
    pixels8 = np.empty((target_count, 8, 8, 4), dtype=np.uint8)
    source_at = target_at = 0
    ranges = []
    for chain in chains:
        mip_ranges = []
        for w, h, data in chain:
            columns, rows = (w + 3) // 4, (h + 3) // 4
            n = columns * rows
            if len(data) != n * 16:
                raise ValueError('ASTC4 mip block geometry mismatch')
            spatial = blocks4[source_at:source_at + n].reshape(rows, columns, 4, 4, 4).transpose(
                0, 2, 1, 3, 4).reshape(rows * 4, columns * 4, 4)[:h, :w]
            source_at += n
            padded = np.pad(spatial, ((0, (-h) % 8), (0, (-w) % 8), (0, 0)), mode='edge')
            n8 = ((w + 7) // 8) * ((h + 7) // 8)
            pixels8[target_at:target_at + n8] = pixels_to_blocks(padded, 8, n8)
            mip_ranges.append((target_at * 16, (target_at + n8) * 16))
            target_at += n8
        ranges.append(mip_ranges)
    del blocks4, rgba
    encoded = encode_blocks(pixels8, 8, srgb, astcenc, preset, temporary)
    return [[encoded[start:end] for start, end in chain] for chain in ranges]


def rebuild_texture(raw, entry, mips, native):
    """Rebuild the target native storage, all tables and matching thumbnail mips."""
    read_texture(raw, entry, native)
    validate_thumbnail(entry)
    changed = dict(entry, format=SOURCE_FORMATS[entry['format']])
    automatic = descriptor(changed)
    automatic.pkg_block_h = 0
    layout = native.layout(automatic)
    if layout.layers != 1 or layout.tileD:
        raise ValueError('unexpected target ASTC8 native layout')
    changed['layoutData'] = (entry['layoutData'] & ~255) | layout.tileH
    desc = descriptor(changed)
    storage = native.swizzle(desc, mips)
    header = bytearray(raw[:36])
    header[4] = changed['format']
    struct.pack_into('<Q', header, 19, changed['layoutData'])
    offsets = native.offsets(desc) + [native.layout(desc).size]
    start = 36 + 8 * desc.levels
    for level in range(desc.levels):
        header.extend(struct.pack('<Ii', offsets[level + 1] - offsets[level],
                                  start + offsets[level] - (40 + 8 * level)))
    if entry.get('thumbnailBase64'):
        first = entry['thumbnailMip']
        retained = mips[first:]
        thumbnail = bytearray(struct.pack('<Ii', len(retained), 4))
        position = 8 + 8 * len(retained)
        for level, mip in enumerate(retained):
            thumbnail.extend(struct.pack('<Ii', len(mip), position - (12 + 8 * level)))
            position += len(mip)
        thumbnail.extend(b''.join(retained))
        changed['thumbnailBase64'] = base64.b64encode(thumbnail).decode('ascii')
    result = bytes(header) + storage
    verified, storage, _ = read_texture(result, changed, native)
    validate_thumbnail(changed)
    for level, mip in enumerate(mips):
        if native.linear(verified, level, storage) != mip:
            raise ValueError('ASTC8 native swizzle/deswizzle roundtrip differs')
    if (changed['width'], changed['height'], changed['levels']) != (entry['width'], entry['height'], entry['levels']):
        raise ValueError('ASTC8 conversion changed dimensions or mip count')
    return result, changed


def read_replacements(work):
    replacements = {}
    manifest = work / 'repl.tsv'
    for line in manifest.read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        parts = line.split('\t')
        if len(parts) != 2:
            raise ValueError('invalid replacement row')
        name = logical_path(parts[0])
        if name in replacements:
            raise ValueError('duplicate ASTC replacement: ' + name)
        source = Path(parts[1])
        if not source.is_absolute() and not source.is_file():
            source = work / source
        if source.is_symlink() or not source.is_file():
            raise ValueError('missing/non-regular ASTC prepared file: ' + str(source))
        replacements[name] = source.resolve()
    if CACHE_PATH not in replacements:
        raise ValueError('ASTC work lacks its descriptor cache replacement')
    return replacements


def prepare(work, output, astcenc, above=128, preset='medium', batch_mib=64):
    work, output, astcenc = Path(work).resolve(), Path(output).resolve(), Path(astcenc).resolve()
    if above < 0 or batch_mib < 1:
        raise ValueError('threshold must be nonnegative and batch MiB must be positive')
    if output == work or output in work.parents or work in output.parents:
        raise ValueError('ASTC8 output must be separate from source work')
    if output.exists():
        raise ValueError('ASTC8 output already exists: ' + str(output))
    if not astcenc.is_file() or not os.access(astcenc, os.X_OK):
        raise ValueError('local executable astcenc is required')
    replacements = read_replacements(work)
    original_cache = replacements[CACHE_PATH].read_bytes()
    entries = json.loads(original_cache)
    names = [logical_path(entry['filename']) for entry in entries]
    if len(set(names)) != len(names):
        raise ValueError('duplicate texture descriptor path')
    selected, skipped = {False: [], True: []}, Counter()
    budget = batch_mib << 20
    for index, (name, entry) in enumerate(zip(names, entries)):
        if entry['format'] not in SOURCE_FORMATS:
            skipped['not ASTC4'] += 1
            continue
        if max(entry['width'], entry['height']) <= above:
            skipped['at or below threshold'] += 1
            continue
        if entry['type'] != 2 or entry['depth'] != 1 or entry['layoutType'] != 3:
            skipped['unsupported non-2D packaged texture'] += 1
            continue
        if name not in replacements:
            raise ValueError('selected ASTC4 descriptor lacks payload: ' + name)
        pixels = sum(((max(1, entry['width'] >> level) + 3) // 4) *
                     ((max(1, entry['height'] >> level) + 3) // 4) * 64 for level in range(entry['levels']))
        # The decode atlas adds at most one partial row. Keep its RGBA array
        # within the requested budget, including this padding.
        blocks = pixels // 64
        columns, rows, _, _ = atlas_geometry(blocks, 4)
        if columns * rows * 64 > budget:
            raise ValueError('one texture exceeds decoded batch budget: ' + name)
        selected[entry['format'] == 81].append((index, name, pixels))
    output.mkdir(parents=True)
    native = NativeLayout(output / 'native-layout')
    started = time.monotonic()
    records, batches = [], 0
    with tempfile.TemporaryDirectory(prefix='astc8-', dir=output) as temporary:
        def flush(items, srgb):
            nonlocal batches
            if not items:
                return
            chains = [item[4] for item in items]
            converted = transcode_batch(chains, srgb, astcenc, preset, temporary)
            for (index, name, source, raw, _), mips in zip(items, converted):
                entry = entries[index]
                result, changed = rebuild_texture(raw, entry, mips, native)
                # Check the source after the conversion as well as recording it;
                # writing an output can never silently clobber a source file.
                source_hash = digest(raw)
                if digest(source.read_bytes()) != source_hash:
                    raise ValueError('ASTC4 source changed during preparation: ' + name)
                destination = output / 'files' / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(result)
                replacements[name] = destination
                entries[index] = changed
                records.append(dict(path=name, source_path=str(source), source_format=entry['format'],
                                    format=changed['format'], width=entry['width'], height=entry['height'],
                                    levels=entry['levels'], source_bytes=len(raw), bytes=len(result),
                                    source_sha256=source_hash, sha256=digest(result),
                                    mip_sha256=[digest(mip) for mip in mips], native_roundtrip_verified=True,
                                    thumbnail_rebuilt=bool(entry.get('thumbnailBase64'))))
            batches += 1
            print(f'ASTC8: {len(records)} textures, {batches} codec batches, '
                  f'{time.monotonic() - started:.1f}s', flush=True)

        for srgb, jobs in selected.items():
            items, decoded_bytes = [], 0
            for index, name, pixels in jobs:
                total_blocks = (decoded_bytes + pixels) // 64
                columns, rows, _, _ = atlas_geometry(total_blocks, 4)
                if items and columns * rows * 64 > budget:
                    flush(items, srgb)
                    items, decoded_bytes = [], 0
                source = replacements[name]
                raw = source.read_bytes()
                try:
                    desc, storage, _ = read_texture(raw, entries[index], native)
                    validate_thumbnail(entries[index])
                    chain = [(max(1, desc.width >> level), max(1, desc.height >> level),
                              native.linear(desc, level, storage)) for level in range(desc.levels)]
                except ValueError as error:
                    raise ValueError(name + ': ' + str(error)) from error
                items.append((index, name, source, raw, chain))
                decoded_bytes += pixels
            flush(items, srgb)
    if replacements[CACHE_PATH].read_bytes() != original_cache:
        raise ValueError('ASTC4 descriptor cache changed during preparation')
    cache = output / 'files' / CACHE_PATH
    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_text(json.dumps(entries, ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
    replacements[CACHE_PATH] = cache
    materialized = Counter()
    for name, source in list(replacements.items()):
        destination = output / 'files' / name
        if source == destination:
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        try:
            os.link(source, destination)
            materialized['hardlinked'] += 1
        except OSError as error:
            if error.errno != errno.EXDEV:
                raise
            shutil.copyfile(source, destination)
            if digest(source.read_bytes()) != digest(destination.read_bytes()):
                raise ValueError('unchanged ASTC file copy differs: ' + name)
            materialized['copied'] += 1
        replacements[name] = destination
    with (output / 'repl.tsv').open('w', encoding='utf-8') as stream:
        for name, source in sorted(replacements.items()):
            stream.write(name + '\t' + str(source.resolve()) + '\n')
    report = dict(schema=1, source_work=str(work), above_source_long_edge=above, preset=preset,
                  source_block='4x4', target_block='8x8', lossy_astc4_recompression=True,
                  dimensions_unchanged=True, mip_counts_unchanged=True, source_files_unchanged=True,
                  selection_excludes_ui=False, decoded_batch_limit_bytes=budget, codec_batches=batches,
                  unchanged_files_materialized=dict(materialized),
                  changed=len(records), skipped=dict(skipped), source_cache_sha256=digest(original_cache),
                  cache_sha256=digest(cache.read_bytes()),
                  changed_source_bytes=sum(record['source_bytes'] for record in records),
                  changed_output_bytes=sum(record['bytes'] for record in records),
                  seconds=round(time.monotonic() - started, 3), records=records)
    write_json(output / 'astc-block-report.json', report)
    write_json(output / 'manifest.json', dict(schema=1, kind='astc8-prepared-work',
               source_work=str(work), report_sha256=digest((output / 'astc-block-report.json').read_bytes()),
               replacements_sha256=digest((output / 'repl.tsv').read_bytes()),
               cache_sha256=report['cache_sha256'], changed=report['changed']))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--astc-work', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--astcenc', required=True, type=Path)
    parser.add_argument('--above', type=int, default=128)
    parser.add_argument('--preset', choices=('fastest', 'fast', 'medium', 'thorough', 'verythorough', 'exhaustive'), default='medium')
    parser.add_argument('--batch-mib', type=int, default=64)
    options = parser.parse_args()
    prepare(options.astc_work, options.out, options.astcenc, options.above, options.preset, options.batch_mib)


if __name__ == '__main__':
    main()
