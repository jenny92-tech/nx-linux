#!/usr/bin/env python3
# INPUT: Prepared 94012 full-3D RomFS with verified ASTC replacement archives.
# OUTPUT: Standalone Chinese/English resources containing each retained payload once.
# POS: Computer-only CASC compaction; keeps world/model/UI data and never edits saves.
"""Compact prepared remastered resources without applying the classic HD deny gate."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import zlib

from compact_character_resources import Source, fresh_location
import casc_overlay as casc


def omission_reason(name):
    if name.startswith('locales/') and name.split('/')[2] not in ('zhcn', 'enus'):
        return 'unused-language'
    if '/video/' in name:
        return 'disabled-video'
    return None


def select(source):
    known = set(source.positions.values())
    if known != set(range(source.co, source.co + source.cs, 25)):
        raise ValueError('unmapped CASC entries must be inventoried before compaction')
    selected = {name for name in source.rows if omission_reason(name) is None}
    # An alias retained by either language or the shared world keeps its bytes.
    positions = {source.positions[name] for name in selected}
    return selected, positions


def verify_encoded(raw, spec, content_size):
    """Check every original encoding boundary, including BLTE block checksums."""
    def inflate(data):
        decoder = zlib.decompressobj()
        decoded = decoder.decompress(data) + decoder.flush()
        if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
            raise ValueError('zlib stream boundary mismatch')
        return decoded
    if spec == 'n':
        decoded = raw
    elif spec == 'z':
        decoded = inflate(raw)
    elif spec.startswith('b:'):
        if len(raw) < 12 or raw[:4] != b'BLTE' or raw[8] != 15:
            raise ValueError('unsupported BLTE header')
        count = int.from_bytes(raw[9:12], 'big')
        header = struct.unpack_from('>I', raw, 4)[0]
        if not count or header != 12 + 24 * count or header > len(raw):
            raise ValueError('BLTE header bounds')
        cursor, blocks = header, []
        for i in range(count):
            encoded, size = struct.unpack_from('>II', raw, 12 + 24 * i)
            block = raw[cursor:cursor + encoded]
            if (not encoded or len(block) != encoded or
                    hashlib.md5(block).digest() != raw[20 + 24 * i:36 + 24 * i]):
                raise ValueError('BLTE block checksum mismatch')
            if block[:1] == b'N':
                data = block[1:]
            elif block[:1] == b'Z':
                data = inflate(block[1:])
            else:
                raise ValueError('unsupported BLTE block mode')
            if len(data) != size:
                raise ValueError('BLTE block size mismatch')
            blocks.append(data)
            cursor += encoded
        if cursor != len(raw):
            raise ValueError('BLTE trailing bytes')
        decoded = b''.join(blocks)
    else:
        raise ValueError('unsupported encoding: ' + spec)
    if len(decoded) != content_size:
        raise ValueError('decoded asset size mismatch')
    return hashlib.md5(decoded).hexdigest()


def compact(root, output):
    source, output = Source(root), Path(output)
    if output.exists() or output.is_symlink():
        raise ValueError('output must be a new directory')
    selected, positions = select(source)
    location = fresh_location(source)
    tvfs, original_tvfs = bytearray(source.tvfs), bytes(source.tvfs)
    data = output / 'data'
    data.mkdir(parents=True)
    archive = data / casc.archive_filename(*location)
    relocated, ledger, content_hashes, encodings = {}, {}, {}, {}
    for name, row in source.rows.items():
        at = source.positions[name]
        if at in encodings and encodings[at] != row[8]:
            raise ValueError('aliased asset encoding mismatch')
        encodings[at] = row[8]
    with archive.open('xb') as stream:
        for number, at in enumerate(sorted(positions), 1):
            raw = source.encoded(at)
            size = struct.unpack_from('>I', tvfs, at + 21)[0]
            content_hashes[at] = verify_encoded(raw, encodings[at], size)
            offset = stream.tell()
            stream.write(raw)
            key = casc.ekey_for(raw, *location, offset)
            tvfs[at:at + 16] = key
            relocated[at] = key
            ledger[at] = (offset, len(raw), hashlib.sha256(raw).hexdigest())
            if number % 10000 == 0:
                print(f'compacted {number}/{len(positions)} entries; {stream.tell():,} bytes', flush=True)

        def append(raw):
            encoded = casc.blte_encode(raw)
            key = casc.ekey_for(encoded, *location, stream.tell())
            stream.write(encoded)
            return key, len(raw), len(encoded)

        vfs_key, vfs_size, vfs_encoded = append(bytes(tvfs))
        key = bytes.fromhex(source.config['vfs-root'].split()[1])
        root_size, root_encoded = map(int, source.config['vfs-root-size'].split())
        root = bytearray(casc.read_data_file(source.root / 'data', key, root_encoded))
        old_key = bytes.fromhex(source.config['vfs-1'].split()[1])
        if len(root) != root_size or root.count(old_key) != 1:
            raise ValueError('unreviewed root manifest')
        at = root.index(old_key)
        root[at:at + 16] = vfs_key
        casc.set_cft_sizes(root, at, vfs_encoded, vfs_size)
        root_key, root_size, root_encoded = append(bytes(root))
    lines = casc.replace_config(source.lines, 'vfs-1', hashlib.md5(tvfs).hexdigest() + ' ' + vfs_key.hex())
    lines = casc.replace_config(lines, 'vfs-1-size', f'{vfs_size} {vfs_encoded}')
    for key in ('vfs-root', 'vfs-2'):
        if source.config[key] != source.config['vfs-root']:
            raise ValueError('unreviewed secondary manifest')
        lines = casc.replace_config(lines, key, hashlib.md5(root).hexdigest() + ' ' + root_key.hex())
        lines = casc.replace_config(lines, key + '-size', f'{root_size} {root_encoded}')
    (data / '.build.config').write_text(''.join(lines))
    with (data / 'build_manifest.psv').open('x') as stream:
        for line in (source.root / 'data/build_manifest.psv').read_text().splitlines():
            if line.startswith(('#', 'FileId!')):
                stream.write(line + '\n')
        for name in sorted(selected):
            at = source.positions[name]
            row = source.rows[name].copy()
            row[2], row[3], row[6] = str(location[0]), str(location[1]), relocated[at].hex()
            row[4], row[5], row[7] = content_hashes[at], str(struct.unpack_from('>I', tvfs, at + 21)[0]), str(ledger[at][1])
            stream.write('|'.join(row) + '\n')
    if casc.load_vfs1(output) != tvfs:
        raise ValueError('rewritten manifest round trip failed')
    allowed = set()
    with archive.open('rb') as stream:
        for at, (offset, length, digest) in ledger.items():
            stream.seek(offset)
            if hashlib.sha256(stream.read(length)).hexdigest() != digest:
                raise ValueError('retained encoded asset round trip failed')
            allowed.update(range(at, at + 16))
    if any(a != b and i not in allowed for i, (a, b) in enumerate(zip(original_tvfs, tvfs))):
        raise ValueError('unexpected non-address manifest change')
    for at in range(source.co, source.co + source.cs, 25):
        if at not in positions and tuple(tvfs[at + 9:at + 11]) == location:
            raise ValueError('omitted asset aliases compact archive')
    omitted = Counter()
    for name in source.rows.keys() - selected:
        omitted[omission_reason(name)] += int(source.rows[name][7])
    return dict(schema=1, complete=True, game_build='94012', languages=['zhcn', 'enus'],
                remastered_3d=True, hd_world_models_ui_retained=True, video_playback=False,
                retained_encoded_bytes_unchanged=True, retained_entries_verified=len(ledger),
                retained_logical_paths=len(selected), omitted_logical_bytes=dict(omitted),
                archive_location=location, archive=archive.name,
                resource_bytes=sum(p.stat().st_size for p in data.iterdir()),
                requires_original_archives=False, runtime_rendering_verified=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    report = compact(args.source, args.out)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report), flush=True)
