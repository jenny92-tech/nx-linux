#!/usr/bin/env python3
# INPUT: User's original 90447 RomFS and verified computer-prepared handheld pack.
# OUTPUT: A small private CASC overlay that disables rain, with complete input/output hashes.
# POS: Computer-only quality option; no downloads, keys, original writes or game execution.
"""Generate a rain-free resource overlay from your own prepared Switch files."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import shutil
import struct
import sys
import zlib

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'tools'))
import casc_overlay as casc
from install_handheld_pack import install, sha256

SOURCE_HASHES = {
    'data/global/excel/levels.txt': '5e53e4e306f6a4fe9008a3ff2f6889d865f08361d33d68f2cde591f20ce79f2b',
    'data/global/excel/levels.bin': '31a1feade185c3a02cfdab6cc00db4c5ecee3f07a3cda04098cd688a4b154565',
}


def disable_rain(text, binary):
    """Cross-check all records; change only Rain in the text and byte 11."""
    parsed = list(csv.DictReader(io.StringIO(text.decode('utf-8')), delimiter='\t'))
    rows = [r for r in parsed if r.get('Id', '').isdigit()]
    if (len(binary) != 4 + 137 * 388 or struct.unpack_from('<I', binary)[0] != 137 or
            len(rows) != 137 or {int(r['Id']) for r in rows} != set(range(137))):
        raise ValueError('requires the complete 137-record 90447 level tables')
    changed = bytearray(binary)
    rain = []
    for row in rows:
        number = int(row['Id'])
        start = 4 + number * 388
        if struct.unpack_from('<I', binary, start)[0] != number:
            raise ValueError('level table ID mismatch')
        for field, offset in (('Act', 9), ('Rain', 11), ('Mud', 12), ('NoPer', 13)):
            value = int(row.get(field, '') or 0)
            if binary[start + offset] != value or (field != 'Act' and value not in (0, 1)):
                raise ValueError('level table text/binary field mismatch: ' + field)
        if int(row['Rain'] or 0):
            rain.append(number)
            changed[start + 11] = 0
    differences = [i for i, (old, new) in enumerate(zip(binary, changed)) if old != new]
    if differences != [4 + i * 388 + 11 for i in sorted(rain)]:
        raise ValueError('unexpected changes outside the Rain field')
    lines = text.decode('utf-8').splitlines(keepends=True)
    header = lines[0].rstrip('\r\n').split('\t')
    id_column, rain_column = header.index('Id'), header.index('Rain')
    selected = set(rain)
    for i, line in enumerate(lines[1:], 1):
        body = line.rstrip('\r\n')
        fields = body.split('\t')
        if (len(fields) > max(id_column, rain_column) and fields[id_column].isdigit() and
                int(fields[id_column]) in selected):
            fields[rain_column] = '0'
            lines[i] = '\t'.join(fields) + line[len(body):]
    return ''.join(lines).encode('utf-8'), bytes(changed), dict(
        rain_rows=sorted(rain), changed_binary_bytes=len(rain), record_count=137,
        record_stride=388, rain_byte_offset=11, all_other_binary_bytes_preserved=True)


def read_asset(base, manifest, name):
    row = manifest[name]
    key = bytes.fromhex(row[6])
    file = base / 'data' / casc.archive_filename(key[9],key[10])
    with file.open('rb') as stream:
        stream.seek(int.from_bytes(key[11:16], 'big'))
        encoded = stream.read(int(row[7]))
    if len(encoded) != int(row[7]):
        raise ValueError('truncated source asset: ' + name)
    value = zlib.decompress(encoded) if row[8] == 'z' else casc.read_data_file(base / 'data', key, int(row[7]))
    if (len(value) != int(row[5]) or hashlib.md5(value).hexdigest() != row[4] or
            hashlib.sha256(value).hexdigest() != SOURCE_HASHES[name]):
        raise ValueError('source asset/version mismatch: ' + name)
    return value


def prepare(base, prepared, out):
    base, prepared, out = base.resolve(), prepared.resolve(), out.resolve()
    if out.exists() or any(p == out or p in out.parents or out in p.parents for p in (base, prepared)):
        raise ValueError('choose a new, separate private output directory')
    install(prepared, base, check_only=True)
    original_manifest = (prepared / 'manifest.json').read_bytes()
    proof = json.loads(original_manifest)
    if proof.get('classic_assets_policy'):
        raise ValueError('use the original prepared pack, before additional quality overlays')
    manifest = casc.read_manifest(base)
    names = tuple(SOURCE_HASHES)
    original = {name: read_asset(base, manifest, name) for name in names}
    text, binary, changes = disable_rain(*(original[n] for n in names))
    payloads = dict(zip(names, (text, binary)))
    out.mkdir(parents=True, mode=0o700)
    source = out / 'work/source'
    (source / 'data').mkdir(parents=True)
    for path in (base / 'data').iterdir():
        if path.is_file():
            (source / 'data' / path.name).symlink_to(path)
    for path in (prepared / 'RomFS/data').iterdir():
        target = source / 'data' / path.name
        if target.is_symlink():
            target.unlink()
        target.symlink_to(path)
    tvfs = casc.load_vfs1(source)
    offset, size = struct.unpack_from('>II', tvfs, 28)
    positions = {n: casc.cft_position(tvfs, offset, offset + size, manifest, n) for n in names}
    replacements = {}
    for name, value in payloads.items():
        path = out / 'work' / Path(name).name
        path.write_bytes(value)
        replacements[name] = path
    rebuilt = out / 'work/rebuilt'
    casc.make_overlay(source, rebuilt, replacements)
    tvfs = casc.load_vfs1(rebuilt)
    for name, position in positions.items():
        key = bytes(tvfs[position:position + 16])
        size = struct.unpack_from('>I', tvfs, position + 16)[0]
        file = rebuilt / 'data' / casc.archive_filename(key[9],key[10])
        with file.open('rb') as stream:
            start = int.from_bytes(key[11:16], 'big')
            stream.seek(start)
            encoded = stream.read(size)
        if (casc.ekey_for(encoded, key[9], key[10], start) != key or
                zlib.decompress(encoded) != payloads[name]):
            raise ValueError('generated CASC round-trip mismatch: ' + name)
    target = out / 'overlay/data'
    target.mkdir(parents=True)
    for path in (rebuilt / 'data').iterdir():
        if path.is_file() and not path.is_symlink():
            shutil.copyfile(path, target / path.name)
    files = {'overlay/data/' + p.name: dict(size=p.stat().st_size, sha256=sha256(p))
             for p in target.iterdir()}
    if len(files) != 2 or 'overlay/data/.build.config' not in files:
        raise ValueError('unexpected generated overlay files')
    result = dict(schema=1, complete=True, game_build='90447', policy='no-rain-v1',
        changes=changes, source_assets_sha256=SOURCE_HASHES,
        source_manifest_sha256=proof['source_manifest_sha256'],
        source_prepared_manifest_sha256=hashlib.sha256(original_manifest).hexdigest(),
        source_prepared_files={r['path']: r for r in proof['files'] if r['path'].startswith('RomFS/data/')},
        output_files=files, roundtrip_verified=True, original_files_modified=False,
        gameplay_verified=False, target_fps_verified=False, game_data_downloaded=False)
    if (prepared / 'manifest.json').read_bytes() != original_manifest:
        raise ValueError('source prepared manifest changed during processing')
    (out / 'complete.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--romfs', type=Path, required=True, help='original Update/RomFS')
    p.add_argument('--prepared', type=Path, required=True, help='original handheld-prepared-128 directory')
    p.add_argument('--out', type=Path, required=True, help='new private directory')
    a = p.parse_args()
    report = prepare(a.romfs, a.prepared, a.out)
    print(json.dumps(dict(complete=report['complete'], changes=report['changes'],
        output=str(a.out), copy_only=['overlay/', 'complete.json'], game_started=False)))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, struct.error) as error:
        sys.exit('classic weather preparation: ' + str(error))
