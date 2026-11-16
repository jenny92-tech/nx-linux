#!/usr/bin/env python3
# INPUT: User-provided NSPs, prod.keys and optional title.keys; existing hactool.
# OUTPUT: Locally merged, explicitly selected build-verified ExeFS/RomFS and hashes.
# POS: Computer-only extraction; no key downloads, uploads or game execution.
"""Extract an explicitly selected, hash-verified Switch D2R build."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'dm/tools'))
from nso_inventory import Nso

MODULES = {
    'main': '55401f8ed5c3ec708292e37df6a498eb4aebc9852998520eb3e5735e61c6a93f',
    'sdk': '1c9f198a91c0bcfee5cf8f5b53fb941895e91334dfb766ce79fc68576797f017',
    'subsdk0': '67a38fcf89cd13e040a021eb3d157072f38ebf6680a907874c35c7b231b5d4e2',
}
BUILDS = {
    '90447': dict(switch_version='1.0.28.0', modules=MODULES),
    '94012': dict(switch_version='1.0.34.0', modules=dict(MODULES,
        main='55fd909bb7230b002357e5b0d8499c11f06888f345c49e69eb4734df44a27d0e')),
}


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for part in iter(lambda: f.read(8 << 20), b''):
            h.update(part)
    return h.hexdigest()


def verify_update(update, game_build='90447'):
    if game_build not in BUILDS:
        raise ValueError('unsupported game build: ' + str(game_build))
    profile = BUILDS[game_build]
    required = f"Switch {profile['switch_version']} / {game_build}"
    modules = {}
    for name, expected in profile['modules'].items():
        path = update / 'ExeFS' / name
        if digest(path) != expected:
            raise ValueError(f'{name}: requires original {required}; unsupported or corrupt input')
        nso = Nso(path)
        if not all(s['sha256_matches'] for s in nso.segments):
            raise ValueError(name + ': NSO segment hash mismatch')
        modules[name] = dict(sha256=expected, build_id=nso.data[0x40:0x60].hex())
    manifest = update / 'RomFS/data/build_manifest.psv'
    with manifest.open(encoding='utf-8') as f:
        prefix = ''.join(f.readline() for _ in range(12))
    if f'## build-name = {game_build}\n' not in prefix:
        raise ValueError('requires RomFS build ' + game_build)
    return dict(game_build=game_build, modules=modules, source_manifest_sha256=digest(manifest))


def pfs_entries(path):
    """Validate the complete PFS0 table before writing anything from an NSP."""
    size = path.stat().st_size
    with path.open('rb') as f:
        header = f.read(16)
        if len(header) != 16 or header[:4] != b'PFS0':
            raise ValueError('expected an uncompressed NSP/PFS0 (NSZ/XCI are not this input format)')
        count, string_size, _ = struct.unpack_from('<III', header, 4)
        start = 16 + count * 24 + string_size
        if not 0 < count <= 100000 or string_size > 16 << 20 or start > size:
            raise ValueError('invalid PFS0 table bounds')
        table, strings = f.read(count * 24), f.read(string_size)
    names, ranges, result = set(), [], []
    for i in range(count):
        off, length, index, reserved = struct.unpack_from('<QQII', table, i * 24)
        end = strings.find(b'\0', index) if index < len(strings) else -1
        if end < 0 or reserved or off + length > size - start:
            raise ValueError('invalid PFS0 entry')
        name = strings[index:end].decode('utf-8')
        if (not name or name in ('.', '..') or '/' in name or '\\' in name or
                ':' in name or any(ord(c) < 32 for c in name) or name.lower() in names):
            raise ValueError('unsafe or duplicate PFS0 filename')
        names.add(name.lower())
        ranges.append((off, off + length))
        result.append((name, start + off, length))
    ordered = sorted(ranges)
    if any(b[0] < a[1] for a, b in zip(ordered, ordered[1:])):
        raise ValueError('overlapping PFS0 entries')
    return result


def unpack(path, destination):
    entries = pfs_entries(path)
    destination.mkdir()
    with path.open('rb') as f:
        for name, offset, size in entries:
            if not name.lower().endswith(('.nca', '.tik')):
                continue
            f.seek(offset)
            target = destination / name
            h, left = hashlib.sha256(), size
            with target.open('xb') as out:
                while left:
                    data = f.read(min(left, 8 << 20))
                    if not data:
                        raise ValueError('truncated NSP payload')
                    out.write(data)
                    h.update(data)
                    left -= len(data)
            if name.lower().endswith('.nca') and h.hexdigest()[:32] != name[:32].lower():
                raise ValueError('NCA content ID/hash mismatch: ' + name)


def ticket_key(data):
    """Common ticket's encrypted title key; personalized tickets need title.keys."""
    if len(data) < 4:
        raise ValueError('truncated ticket')
    signature = int.from_bytes(data[:4], 'little')
    body = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80,
            0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}.get(signature)
    if body is None or len(data) < body + 0x170:
        raise ValueError('unsupported/truncated ticket')
    if data[body + 0x141] != 0:
        return None
    return data[body + 0x160:body + 0x170].hex(), data[body + 0x40:body + 0x50].hex()


def read_title_keys(path):
    keys = {}
    for line in path.read_text(encoding='utf-8').splitlines():
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        match = re.fullmatch(r'([0-9a-fA-F]{32})\s*=\s*([0-9a-fA-F]{32})', line)
        if not match:
            raise ValueError('malformed title.keys (values suppressed)')
        key, value = (s.lower() for s in match.groups())
        if key in keys and keys[key] != value:
            raise ValueError('conflicting title.keys entries (values suppressed)')
        keys[key] = value
    return keys


def extract(packages, prod_keys, title_keys, hactool, output, game_build='90447'):
    if game_build not in BUILDS:
        raise ValueError('unsupported game build: ' + str(game_build))
    if output.exists():
        raise ValueError('output must be a new directory')
    if not prod_keys.is_file():
        raise ValueError('provide your local prod.keys')
    output.mkdir(parents=True)
    os.chmod(output, 0o700)
    # Never print hactool's raw diagnostics or serialize any key material.
    # Its exit code alone is insufficient: invalid headers can return zero.
    with tempfile.TemporaryDirectory(prefix='.extract-', dir=output) as temp:
        temp = Path(temp)
        keys = read_title_keys(title_keys) if title_keys else {}
        folders = []
        for number, package in enumerate(packages):
            print(f'extract: container {number + 1}/{len(packages)}', flush=True)
            folder = temp / f'package-{number}'
            unpack(package, folder)
            folders.append(folder)
            for ticket in folder.glob('*.tik'):
                item = ticket_key(ticket.read_bytes())
                if item:
                    key, value = item
                    if key in keys and keys[key] != value:
                        raise ValueError('conflicting ticket/title.keys (values suppressed)')
                    keys[key] = value
        # hactool reads HOME first. Refuse hidden precedence rather than silently
        # depending on somebody's pre-existing key store or replacing their HOME.
        home_keys = Path.home() / '.switch/title.keys'
        if home_keys.is_file() and read_title_keys(home_keys) != keys:
            raise ValueError('hactool would prefer ~/.switch/title.keys; use a clean OS account or matching key store')
        config = temp / 'config/switch'
        config.mkdir(parents=True)
        secret = config / 'title.keys'
        secret.write_text(''.join(f'{key} = {value}\n' for key, value in sorted(keys.items())))
        secret.chmod(0o600)
        env = dict(os.environ, XDG_CONFIG_HOME=str(config.parent))

        def ht(*args):
            result = subprocess.run([str(hactool), '-k', str(prod_keys), '--disablekeywarns',
                                     '--suppresskeys', *map(str, args)], env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            text = result.stdout.decode('utf-8', errors='replace')
            if result.returncode or 'Invalid NCA header' in text or '[FAIL]' in text:
                raise ValueError('hactool failed: check supplied keys, tickets and dump integrity (raw key-sensitive output suppressed)')
            return text

        base, patch = [], []
        for folder in folders:
            for nca in sorted(folder.glob('*.nca')):
                info = ht('-t', 'nca', nca)
                if not re.search(r'^Content Type:\s+Program\s*$', info, re.M):
                    continue
                if not re.search(r'^Title ID:\s+0100726014352000\s*$', info, re.M | re.I):
                    continue
                (patch if 'BKTR' in info or 'Patch RomFS' in info else base).append(nca)
        if len(base) != 1 or len(patch) != 1:
            raise ValueError('need exactly one D2R base Program and one ' +
                             BUILDS[game_build]['switch_version'] + ' update Program')
        update = output / 'Update'
        update.mkdir()
        print('extract: merge base + update on computer', flush=True)
        ht('-t', 'nca', '--basenca=' + str(base[0]), '--exefsdir=' + str(update / 'ExeFS'),
           '--romfsdir=' + str(update / 'RomFS'), patch[0])
        report = verify_update(update, game_build)
        report.update(hactool_sha256=digest(hactool), key_values_recorded=False,
                      keys_sent_to_device=False, game_started=False,
                      packages=[dict(size=p.stat().st_size, sha256=digest(p)) for p in packages])
        (output / 'extraction.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, action='append', required=True, help='repeat for base NSP and update NSP; combined NSP also accepted')
    parser.add_argument('--keys', type=Path, required=True)
    parser.add_argument('--title-keys', type=Path)
    parser.add_argument('--hactool', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--game-build', choices=tuple(BUILDS), default='90447',
                        help='explicit original module/resource identity; default preserves the tested 90447 workflow')
    args = parser.parse_args()
    try:
        extract([p.resolve() for p in args.package], args.keys.resolve(),
                args.title_keys.resolve() if args.title_keys else None,
                args.hactool.resolve(), args.out.resolve(), args.game_build)
    except (ValueError, OSError) as error:
        parser.exit(1, f'extract: {error}\n')
    print(f"verified: Switch {BUILDS[args.game_build]['switch_version']} / build {args.game_build}; keys stayed on computer")


if __name__ == '__main__':
    main()
