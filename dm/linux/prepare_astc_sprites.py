#!/usr/bin/env python3
# INPUT: Complete SpA1 sprite work/repl.tsv, native NVN code and local astcenc.
# OUTPUT: Separate ASTC8 sprite work, exact geometry and source/native proofs.
# POS: Host-only atlas conversion; preserves frame widths, origins and animation.
"""Convert supported packaged SpA1 atlases strictly above the source-edge limit."""
import argparse
import errno
import io
import os
import shutil
import struct
import tempfile
import time
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image

from handheld_mips import Descriptor, NativeLayout, logical_path
from prepare_astc_blocks import (atlas_geometry, digest, encode_blocks,
                                 pixels_to_blocks, transcode_batch, write_json)

NVN_FORMATS = {31: 0x25, 32: 0x38, 61: 0x44, 62: 0x48,
               95: 0x79, 81: 0x87, 102: 0x80, 88: 0x8e}
TARGET_FORMATS = {31: 102, 32: 88, 61: 102, 62: 88, 95: 102, 81: 88}
SRGB_FORMATS = {32, 62, 81, 88}


def sprite_header(header):
    """Read only proven SpA1 fields; frameWidth*frames need not equal atlasWidth."""
    if len(header) < 40 or header[:4] != b'SpA1':
        raise ValueError('unsupported/truncated SpA1 header')
    fmt, frame_width, width, height, origin_x, origin_y, frames = struct.unpack_from('<HHIIhhH', header, 4)
    if not 0 < width <= 65536 or not 0 < height <= 65536 or not frame_width or not frames:
        raise ValueError('invalid SpA1 atlas/frame geometry')
    if header[22] != 3:
        raise ValueError('only mode3 packaged SpA1 sprites are supported')
    if header[23] > 5:
        raise ValueError('invalid SpA1 packaged block height')
    size, relative = struct.unpack_from('<Ii', header, 32)
    if not size or relative != 4:
        raise ValueError('invalid SpA1 payload size/relative pointer')
    return dict(format=fmt, frame_width=frame_width, atlas_width=width, height=height,
                origin_x=origin_x, origin_y=origin_y, frames=frames, mode=header[22],
                layout_hex=header[23:31].hex(), payload_bytes=size)


def sprite_descriptor(info, block_height):
    return Descriptor(1, NVN_FORMATS[info['format']], 0, 0,
                      info['atlas_width'], info['height'], 1, 1, 0, block_height + 1)


def read_sprite(raw, native):
    info = sprite_header(raw)
    if len(raw) != 40 + info['payload_bytes']:
        raise ValueError('SpA1 payload length differs from file size')
    if info['format'] not in NVN_FORMATS:
        return info, None, raw[40:]
    desc = sprite_descriptor(info, raw[23])
    layout = native.layout(desc)
    if (layout.layers != 1 or layout.tileD or native.offsets(desc) != [0] or
            layout.size != info['payload_bytes']):
        raise ValueError('SpA1 payload differs from native NVN layout')
    return info, desc, raw[40:]


def decode_bc3(linear, width, height):
    """Use Pillow's existing DDS decoder on native-deswizzled DXT5 blocks."""
    if len(linear) != ((width + 3) // 4) * ((height + 3) // 4) * 16:
        raise ValueError('BC3 sprite block geometry mismatch')
    header = [124, 0x81007, height, width, len(linear), 0, 1] + [0] * 11
    header += [32, 4, struct.unpack('<I', b'DXT5')[0], 0, 0, 0, 0, 0]
    header += [0x1000, 0, 0, 0, 0]
    with Image.open(io.BytesIO(b'DDS ' + struct.pack('<31I', *header) + linear)) as image:
        return np.array(image.convert('RGBA'))


def direct_batch(items, srgb, astcenc, preset, temporary):
    """RGBA8 and BC3 atlases share one target ASTC8 codec invocation."""
    count = sum(((info['atlas_width'] + 7) // 8) * ((info['height'] + 7) // 8)
                for info, _ in items)
    pixels = np.empty((count, 8, 8, 4), dtype=np.uint8)
    ranges, offset = [], 0
    for info, linear in items:
        width, height = info['atlas_width'], info['height']
        if info['format'] in (31, 32):
            if len(linear) != width * height * 4:
                raise ValueError('RGBA8 sprite geometry mismatch')
            rgba = np.frombuffer(linear, dtype=np.uint8).reshape(height, width, 4)
        else:
            rgba = decode_bc3(linear, width, height)
        padded = np.pad(rgba, ((0, (-height) % 8), (0, (-width) % 8), (0, 0)), mode='edge')
        n = ((width + 7) // 8) * ((height + 7) // 8)
        pixels[offset:offset + n] = pixels_to_blocks(padded, 8, n)
        ranges.append((offset * 16, (offset + n) * 16))
        offset += n
    encoded = encode_blocks(pixels, 8, srgb, astcenc, preset, temporary)
    return [encoded[start:end] for start, end in ranges]


def rebuild_sprite(raw, linear8, native):
    info, desc, _ = read_sprite(raw, native)
    if desc is None or info['format'] not in TARGET_FORMATS:
        raise ValueError('unsupported source SpA1 format for ASTC8 conversion')
    target_info = dict(info, format=TARGET_FORMATS[info['format']])
    target_desc = sprite_descriptor(target_info, 0)
    target_desc.pkg_block_h = 0
    automatic = native.layout(target_desc)
    target_desc.pkg_block_h = automatic.tileH + 1
    storage = native.swizzle(target_desc, [linear8])
    header = bytearray(raw[:32])
    struct.pack_into('<H', header, 4, target_info['format'])
    header[23] = automatic.tileH
    result = bytes(header) + struct.pack('<Ii', len(storage), 4) + storage
    changed, verified, packed = read_sprite(result, native)
    if (result[:4] != raw[:4] or result[6:23] != raw[6:23] or result[24:32] != raw[24:32]):
        raise ValueError('SpA1 atlas/frame/origin/animation metadata changed')
    if native.linear(verified, 0, packed) != linear8:
        raise ValueError('ASTC8 SpA1 native swizzle/deswizzle roundtrip differs')
    return result, changed


def read_replacements(work):
    result = {}
    for line in (work / 'repl.tsv').read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        parts = line.split('\t')
        if len(parts) != 2:
            raise ValueError('invalid sprite replacement row')
        name, source = logical_path(parts[0]), Path(parts[1])
        if name in result:
            raise ValueError('duplicate sprite replacement: ' + name)
        if not source.is_absolute() and not source.is_file():
            source = work / source
        if source.is_symlink() or not source.is_file():
            raise ValueError('missing/non-regular prepared file: ' + str(source))
        result[name] = source.resolve()
    return result


def link_or_copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        os.link(source, destination)
        return 'hardlinked'
    except OSError as error:
        if error.errno != errno.EXDEV:
            raise
        shutil.copyfile(source, destination)
        if digest(source.read_bytes()) != digest(destination.read_bytes()):
            raise ValueError('unchanged sprite copy differs: ' + str(source))
        return 'copied'


def batch_bytes(jobs):
    blocks8 = sum(((info['atlas_width'] + 7) // 8) * ((info['height'] + 7) // 8)
                  for _, _, info, _ in jobs)
    columns, rows, _, _ = atlas_geometry(blocks8, 8)
    target = columns * rows * 256
    if jobs[0][2]['format'] not in (95, 81):
        return target
    blocks4 = sum(((info['atlas_width'] + 3) // 4) * ((info['height'] + 3) // 4)
                  for _, _, info, _ in jobs)
    columns, rows, _, _ = atlas_geometry(blocks4, 4)
    return max(target, columns * rows * 64)


def prepare(work, output, astcenc, above=128, preset='medium', batch_mib=64):
    work, output, astcenc = Path(work).resolve(), Path(output).resolve(), Path(astcenc).resolve()
    if above < 0 or batch_mib < 1:
        raise ValueError('threshold must be nonnegative and batch MiB must be positive')
    if output == work or output in work.parents or work in output.parents:
        raise ValueError('sprite output must be separate from source work')
    if output.exists():
        raise ValueError('sprite output already exists: ' + str(output))
    if not astcenc.is_file() or not os.access(astcenc, os.X_OK):
        raise ValueError('local executable astcenc is required')
    replacements = read_replacements(work)
    output.mkdir(parents=True)
    native = NativeLayout(output / 'native-layout')
    budget, started = batch_mib << 20, time.monotonic()
    groups, records, kept = {}, {}, Counter()
    for name, source in replacements.items():
        if not name.endswith('.sprite'):
            continue
        raw = source.read_bytes()
        try:
            info, desc, _ = read_sprite(raw, native)
        except ValueError as error:
            raise ValueError(name + ': ' + str(error)) from error
        record = dict(path=name, source_path=str(source), source_sha256=digest(raw),
                      sha256=digest(raw), source_bytes=len(raw), bytes=len(raw),
                      geometry={key: info[key] for key in ('frame_width', 'atlas_width', 'height', 'origin_x', 'origin_y', 'frames')},
                      source_format=info['format'], format=info['format'],
                      source_layout_hex=info['layout_hex'], layout_hex=info['layout_hex'],
                      source_native_layout_verified=desc is not None, changed=False)
        records[name] = record
        if info['format'] not in TARGET_FORMATS:
            reason = 'already ASTC8' if info['format'] in (102, 88) else f'unsupported format {info["format"]}'
        elif max(info['atlas_width'], info['height']) <= above:
            reason = 'at or below threshold'
        else:
            key = (info['format'] in SRGB_FORMATS, info['format'] in (95, 81))
            job = (name, source, info, record['source_sha256'])
            if batch_bytes([job]) > budget:
                raise ValueError('one sprite exceeds decoded batch budget: ' + name)
            groups.setdefault(key, []).append(job)
            continue
        record['preserved_reason'] = reason
        kept[reason] += 1
    batches, changed = 0, 0
    with tempfile.TemporaryDirectory(prefix='astc8-sprites-', dir=output) as temporary:
        def flush(jobs, srgb, astc4):
            nonlocal batches, changed
            if not jobs:
                return
            source_data, items = [], []
            for name, source, info, source_hash in jobs:
                raw = source.read_bytes()
                if digest(raw) != source_hash:
                    raise ValueError('source sprite changed during preparation: ' + name)
                _, desc, storage = read_sprite(raw, native)
                linear = native.linear(desc, 0, storage)
                source_data.append(raw)
                items.append((info, linear))
            if astc4:
                chains = [[(info['atlas_width'], info['height'], linear)] for info, linear in items]
                targets = [chain[0] for chain in transcode_batch(chains, srgb, astcenc, preset, temporary)]
            else:
                targets = direct_batch(items, srgb, astcenc, preset, temporary)
            for (name, source, info, source_hash), raw, linear8 in zip(jobs, source_data, targets):
                result, target_info = rebuild_sprite(raw, linear8, native)
                if digest(source.read_bytes()) != source_hash:
                    raise ValueError('source sprite changed during preparation: ' + name)
                destination = output / 'files' / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(result)
                replacements[name] = destination
                records[name].update(changed=True, format=target_info['format'], sha256=digest(result),
                                     bytes=len(result), layout_hex=target_info['layout_hex'],
                                     linear_astc8_sha256=digest(linear8), native_roundtrip_verified=True,
                                     geometry_and_animation_bytes_unchanged=True,
                                     lossy_astc4_recompression=info['format'] in (95, 81))
                changed += 1
            batches += 1
            print(f'ASTC8 sprites: {changed} converted, {batches} codec batches, '
                  f'{time.monotonic() - started:.1f}s', flush=True)

        for (srgb, astc4), jobs in groups.items():
            pending = []
            for job in jobs:
                if pending and batch_bytes(pending + [job]) > budget:
                    flush(pending, srgb, astc4)
                    pending = []
                pending.append(job)
            flush(pending, srgb, astc4)
    materialized = Counter()
    for name, source in list(replacements.items()):
        destination = output / 'files' / name
        if source == destination:
            continue
        materialized[link_or_copy(source, destination)] += 1
        if name in records and digest(source.read_bytes()) != records[name]['source_sha256']:
            raise ValueError('preserved source sprite changed during preparation: ' + name)
        replacements[name] = destination
    with (output / 'repl.tsv').open('w', encoding='utf-8') as stream:
        for name, source in sorted(replacements.items()):
            stream.write(name + '\t' + str(source.resolve()) + '\n')
    report = dict(schema=1, source_work=str(work), above_source_atlas_long_edge=above,
                  target_block='8x8', preset=preset, decoded_batch_limit_bytes=budget,
                  dimensions_and_animation_unchanged=True, alpha_channel_preserved=True,
                  source_files_unchanged=True, changed=changed, sprites=len(records),
                  preserved=dict(kept), unchanged_files_materialized=dict(materialized),
                  codec_batches=batches, seconds=round(time.monotonic() - started, 3),
                  source_replacements_sha256=digest((work / 'repl.tsv').read_bytes()),
                  records=[records[name] for name in sorted(records)])
    write_json(output / 'sprite-report.json', report)
    write_json(output / 'manifest.json', dict(schema=1, kind='astc8-prepared-sprites',
               source_work=str(work), changed=changed,
               report_sha256=digest((output / 'sprite-report.json').read_bytes()),
               replacements_sha256=digest((output / 'repl.tsv').read_bytes())))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sprite-work', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--astcenc', required=True, type=Path)
    parser.add_argument('--above', type=int, default=128)
    parser.add_argument('--preset', choices=('fastest', 'fast', 'medium', 'thorough', 'verythorough', 'exhaustive'), default='medium')
    parser.add_argument('--batch-mib', type=int, default=64)
    options = parser.parse_args()
    prepare(options.sprite_work, options.out, options.astcenc, options.above, options.preset, options.batch_mib)


if __name__ == '__main__':
    main()
