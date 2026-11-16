#!/usr/bin/env python3
# INPUT:  Prepared 94012 RomFS, classic resource policy and menu asset references
# OUTPUT: Standalone smaller RomFS with unchanged retained encoded assets
# POS:    Offline character-menu resource pruning; never edits native code or saves
import argparse
from collections import Counter, deque
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'tools'))
import casc_overlay as casc
from classic_resource_gate import PRESERVED_PREFIXES, PRESERVED_METADATA
from compact_classic import omission_reason

PLAYER_PREFIXES = ('data/hd/character/player/', 'data/hd/character/common/', 'data/hd/items/')
OPAQUE_MENU_METADATA = {'data/hd/character/monsters.json', 'data/hd/missiles/missiles.json',
                        'data/hd/objects/objects.json', 'data/hd/objects/vfx_hd_exclusions.json'}
SCAN_EXTENSIONS = {'.json', '.model', '.particles', '.skeleton', '.animations', '.timelines',
                   '.physics', '.variantdata', '.objecteffects', '.cloth', '.params', '.sprite',
                   '.frontend'}
PATH = re.compile(rb'(?=(data[\\/][a-zA-Z0-9_./\\ @()+,\[\]-]+))', re.IGNORECASE)
UTF16 = re.compile(rb'(?:[\x20-\x7e]\x00){8,}')


def references(content):
    texts = [content]
    texts.extend(match.group().decode('utf-16le').encode() for match in UTF16.finditer(content))
    return {re.sub('/+', '/', match.group(1).decode().replace('\\', '/')).lower().rstrip()
            for text in texts for match in PATH.finditer(text)}


def is_menu_root(name):
    return (name.startswith(PLAYER_PREFIXES) or name.startswith('data/hd/env/preset/ui/') or
            name.startswith('data/hd/env/ibl/') or
            name.startswith('data/hd/env/vis/ui_frontend_') or
            name == 'data/hd/env/biome/ui_characterselectscreen.json')


class Source:
    def __init__(self, root):
        self.root = Path(root)
        self.lines, self.config = casc.config_values(self.root / 'data/.build.config')
        if self.config.get('build-name') != '94012':
            raise ValueError('character-menu pruning requires game build 94012')
        # INDEX and the internal TVFS "data" row describe the manifest itself,
        # not files in its container table. Inventory only actual asset paths.
        self.rows = {name: row for name, row in casc.read_manifest(self.root).items()
                     if row[7].isdigit() and name.startswith(('data/', 'locales/'))}
        self.tvfs = casc.load_vfs1(self.root)
        self.co, self.cs = struct.unpack_from('>II', self.tvfs, 28)
        if self.cs % 25 or self.co + self.cs > len(self.tvfs):
            raise ValueError('invalid container table')
        positions = {}
        for at in range(self.co, self.co + self.cs, 25):
            key = bytes(self.tvfs[at:at + 16]).hex()
            if key in positions:
                raise ValueError('ambiguous container key')
            positions[key] = at
        self.positions = {}
        for name, row in self.rows.items():
            at = positions.get(row[6])
            self.positions[name] = at if at is not None else casc.cft_position(
                self.tvfs, self.co, self.co + self.cs, self.rows, name)

    def encoded(self, at):
        key = bytes(self.tvfs[at:at + 16])
        size = struct.unpack_from('>I', self.tvfs, at + 16)[0]
        offset = int.from_bytes(key[11:16], 'big')
        # Older desktop extractions used decimal names. Both spellings can
        # exist, so accept a candidate only after its stored EKey matches.
        names = dict.fromkeys((casc.archive_filename(key[9], key[10]),
                               f'{key[9]:02d}-{key[10]:08d}.data'))
        for name in names:
            path = self.root / 'data' / name
            if not path.is_file():
                continue
            with path.open('rb') as stream:
                stream.seek(offset)
                raw = stream.read(size)
            if len(raw) == size and casc.ekey_for(raw, key[9], key[10], offset) == key:
                return raw
        raise ValueError(f'source asset checksum mismatch at CFT {at:#x}')

    def decoded(self, name):
        at = self.positions[name]
        raw = self.encoded(at)
        spec = self.rows[name][8]
        if spec == 'z':
            return zlib.decompress(raw)
        if spec == 'n':
            return raw
        # Decode the already verified candidate rather than reopening a guessed
        # canonical name that may belong to a different legacy decimal chunk.
        if raw[:4] != b'BLTE' or len(raw) < 12:
            raise ValueError('unsupported encoded dependency')
        cursor = struct.unpack_from('>I', raw, 4)[0]
        blocks = []
        for i in range(int.from_bytes(raw[9:12], 'big')):
            encoded, size = struct.unpack_from('>II', raw, 12 + i * 24)
            block = raw[cursor:cursor + encoded]
            if not block or hashlib.md5(block).digest() != raw[20 + i * 24:36 + i * 24]:
                raise ValueError('dependency block checksum mismatch')
            decoded = block[1:] if block[:1] == b'N' else zlib.decompress(block[1:]) if block[:1] == b'Z' else None
            if decoded is None or len(decoded) != size:
                raise ValueError('dependency block encoding/size mismatch')
            blocks.append(decoded)
            cursor += encoded
        if cursor != len(raw):
            raise ValueError('dependency trailing encoded bytes')
        return b''.join(blocks)


def select(source):
    classic = {name for name in source.rows if omission_reason(name, PRESERVED_PREFIXES, PRESERVED_METADATA) is None}
    roots = {name for name in source.rows if is_menu_root(name)}
    # The frontend opens named lighting defaults/SH sidecars and shared LOD
    # catalogs implicitly. Keep catalogs opaque instead of traversing every
    # world asset listed in them.
    shared = {name for name in source.rows if name.startswith(('data/hd/global/', 'data/hd/env/vis/'))
              or name in OPAQUE_MENU_METADATA}
    selected, scanned, unavailable = classic | roots | shared, set(), set()
    compiled = {}
    for name in source.rows:
        model = re.fullmatch(r'(.+)_lod\d+\.model', name)
        if model:
            compiled.setdefault(model[1] + '.model', set()).add(name)
        if name.endswith(('/combined.animations', '/combined.timelines')):
            compiled.setdefault(str(Path(name).parent), set()).add(name)
    pending = deque(sorted(roots))
    while pending:
        name = pending.popleft()
        if name in scanned or Path(name).suffix not in SCAN_EXTENSIONS:
            continue
        scanned.add(name)
        for target in references(source.decoded(name)):
            # Binary string lengths can be printable bytes between adjacent
            # paths. Match actual manifest names before interpreting that tail.
            prefixes = [target[:end] for end in range(1, len(target) + 1)
                        if target[:end] in source.rows or target[:end] in compiled or
                        (target[:end].endswith('.animation') and
                         str(Path(target[:end]).parent) in compiled)]
            if prefixes:
                target = prefixes[-1]
            targets = ({target} if target in source.rows else set()) | compiled.get(target, set())
            if not targets and target.endswith('.animation'):
                targets = compiled.get(str(Path(target).parent), set())
            if not targets:
                unavailable.add(target)
            for dependency in targets - selected:
                selected.add(dependency)
                pending.append(dependency)
    # Preserve all logical aliases of an encoded asset that is still required.
    positions = {source.positions[name] for name in selected}
    selected.update(name for name, at in source.positions.items() if at in positions)
    return selected, dict(classic_paths=len(classic), menu_roots=len(roots),
                         dependency_files_scanned=len(scanned), unavailable_in_source=sorted(unavailable))


def fresh_location(source):
    used = {tuple(source.tvfs[at + 9:at + 11]) for at in range(source.co, source.co + source.cs, 25)}
    for key in ('vfs-1', 'vfs-root', 'vfs-2'):
        used.add(tuple(bytes.fromhex(source.config[key].split()[1])[9:11]))
    for archive in range(1, 256):
        if (0, archive) not in used and not (source.root / 'data' / casc.archive_filename(0, archive)).exists():
            return 0, archive
    raise ValueError('no unused archive location')


def compact(root, output):
    source, output = Source(root), Path(output)
    if output.exists() or output.is_symlink():
        raise ValueError('output must be a new directory')
    selected, selection = select(source)
    positions = {source.positions[name] for name in selected}
    location = fresh_location(source)
    tvfs = bytearray(source.tvfs)
    data = output / 'data'
    data.mkdir(parents=True)
    archive = data / casc.archive_filename(*location)
    relocated, ledger = {}, {}
    with archive.open('xb') as stream:
        for at in sorted(positions):
            raw = source.encoded(at)
            offset = stream.tell()
            stream.write(raw)
            key = casc.ekey_for(raw, *location, offset)
            tvfs[at:at + 16] = key
            relocated[at] = key
            ledger[at] = (offset, len(raw), hashlib.sha256(raw).hexdigest())

        def append(raw):
            encoded = casc.blte_encode(raw)
            key = casc.ekey_for(encoded, *location, stream.tell())
            stream.write(encoded)
            return key, len(raw), len(encoded)

        vfs_key, vfs_size, vfs_encoded = append(bytes(tvfs))
        key = bytes.fromhex(source.config['vfs-root'].split()[1])
        root_size, root_encoded = map(int, source.config['vfs-root-size'].split())
        root = bytearray(casc.read_data_file(source.root / 'data', key, root_encoded))
        old_vfs_key = bytes.fromhex(source.config['vfs-1'].split()[1])
        if len(root) != root_size or root.count(old_vfs_key) != 1:
            raise ValueError('unreviewed root manifest')
        at = root.index(old_vfs_key)
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
            row = source.rows[name].copy()
            row[2], row[3], row[6] = str(location[0]), str(location[1]), relocated[source.positions[name]].hex()
            row[7] = str(ledger[source.positions[name]][1])
            stream.write('|'.join(row) + '\n')
    # Retained data must round trip byte-for-byte; omitted entries cannot alias it.
    if casc.load_vfs1(output) != tvfs:
        raise ValueError('rewritten manifest round trip failed')
    allowed = set()
    with archive.open('rb') as stream:
        for at, (offset, length, expected) in ledger.items():
            stream.seek(offset)
            if hashlib.sha256(stream.read(length)).hexdigest() != expected:
                raise ValueError('retained asset round trip failed')
            allowed.update(range(at, at + 16))
    if any(a != b and i not in allowed for i, (a, b) in enumerate(zip(source.tvfs, tvfs))):
        raise ValueError('unexpected non-address manifest change')
    for at in range(source.co, source.co + source.cs, 25):
        if at not in positions and tuple(tvfs[at + 9:at + 11]) == location:
            raise ValueError('omitted asset aliases the compact archive')
    removed = Counter()
    for name in source.rows.keys() - selected:
        removed['/'.join(name.split('/')[:3])] += int(source.rows[name][7])
    report = dict(complete=True, game_build='94012', selected_logical_paths=len(selected),
                  retained_encoded_entries=len(ledger), retained_encoded_bytes=sum(x[1] for x in ledger.values()),
                  retained_encoded_bytes_unchanged=True, removed_by_category=dict(removed),
                  output_bytes=sum(p.stat().st_size for p in data.iterdir()),
                  archive_location=location, runtime_rendering_verified=False, **selection)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    report = compact(args.source, args.out)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: value for key, value in report.items() if key != 'unavailable_in_source'}))
