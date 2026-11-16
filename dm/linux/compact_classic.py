#!/usr/bin/env python3
# INPUT: A verified flat-menu classic bundle, including its original CASC fallback.
# OUTPUT: Standalone Chinese/English classic resources with byte-identical retained assets.
# POS: Desktop-only repacking; installer verification needs no source game or CASC tools.
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys

POLICY = 'classic-zhcn-enus-v1'
LOCATION = (0, 8)  # Keep manifests in the always-mounted base chunk, like the verified frontend.


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(8 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n')


def omission_reason(name, prefixes, metadata):
    if name.startswith('data/hd/'):
        relative = name[len('data/hd/'):]
        if not (any(relative.startswith(prefix) for prefix in prefixes) or relative in metadata):
            return 'native-gated-hd-world'
    if name.startswith('locales/') and name.split('/')[2] not in ('zhcn', 'enus'):
        return 'unused-language'
    if '/video/' in name:
        return 'disabled-video'
    return None


def verify_compact(game, manifest, device):
    report = json.loads((game/'compact-classic.json').read_text())
    if (device['id'] != 'rk3566-2gb' or report.get('schema') != 1 or
            report.get('policy') != POLICY or report.get('complete') is not True or
            report.get('game_build') != manifest.get('game_build','90447') or report.get('device_profile_sha256') != device['profile_sha256'] or
            report.get('languages') != ['zhcn', 'enus'] or report.get('video_playback') is not False or
            report.get('retained_encoded_bytes_unchanged') is not True or
            report.get('retained_entries_verified', 0) <= 0 or report.get('requires_original_archives') is not False):
        raise ValueError('unreviewed or incomplete compact classic resources')
    native = json.loads((game/'classic-native-v6/classic-native.json').read_text())
    frontend = json.loads((game/'classic-frontend/complete.json').read_text())
    from device_profiles import verify_components
    verify_components(device, native, frontend)
    if native.get('resource_load_policy', {}).get('mode') != 3:
        raise ValueError('compact classic resources require the native deny gate')
    if ('compact-classic.json' not in manifest['files'] or
            report.get('resources') != {name: value for name, value in manifest['files'].items()
                                        if name.startswith('Update/RomFS/')}):
        raise ValueError('compact resource manifest mismatch')
    if set(report['resources']) != {'Update/RomFS/data/.build.config',
            'Update/RomFS/data/build_manifest.psv', 'Update/RomFS/data/00-00000008.data'}:
        raise ValueError('compact archives must use the mounted base chunk')
    return report


def compact(source, out):
    # CASC parsing belongs to desktop preparation, never to the handheld installer.
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent/'tools'))
    import casc_overlay as casc
    from classic_resource_gate import PRESERVED_PREFIXES, PRESERVED_METADATA
    from classic_bundle import compact_descriptor, load_bundle
    from device_profiles import verify_components
    from install_game import verify_game, verify_classic_frontend, verify_classic_modules

    source = source.resolve()
    game = source/'game'
    manifest = verify_game(game)
    game_build = manifest['game_build']
    bundle = load_bundle(game, manifest)
    device = bundle.get('_device')
    if not device or device['id'] != 'rk3566-2gb' or bundle.get('_compact'):
        raise ValueError('requires a full verified flat 2 GB bundle')
    native = verify_classic_modules(bundle['modules'])
    verify_classic_frontend(bundle['frontend'], game/'handheld-prepared-128')
    frontend = json.loads((bundle['frontend']/'complete.json').read_text())
    verify_components(device, native, frontend)
    if native['resource_load_policy']['mode'] != 3:
        raise ValueError('native classic gate must deny unused HD resources')
    roots = [bundle['frontend']/'overlay', game/'handheld-prepared-128/RomFS', game/'Update/RomFS']
    config_lines, config = casc.config_values(roots[0]/'data/.build.config')
    tvfs = casc.load_vfs1(roots[0])
    co, cs = struct.unpack_from('>II', tvfs, 28)
    if cs % 25 or co+cs > len(tvfs):
        raise ValueError('unreviewed CASC container table')
    positions = {}
    for at in range(co, co+cs, 25):
        key = bytes(tvfs[at:at+16])
        if key in positions:
            raise ValueError('ambiguous CASC container key')
        positions[key] = at
    rows = {name: row for name, row in casc.read_manifest(roots[-1]).items()
            if row[7].isdigit() and name.startswith(('data/', 'locales/'))}
    selected, omitted, mapped = {}, Counter(), {}
    for name, row in rows.items():
        reason = omission_reason(name, PRESERVED_PREFIXES, PRESERVED_METADATA)
        if reason:
            omitted[reason] += int(row[7])
            continue
        old = bytes.fromhex(row[6])
        at = positions.get(old)
        if at is None:
            at = casc.cft_position(tvfs, co, co+cs, rows, name)
        selected[name] = at
        mapped.setdefault(at, []).append(name)
    if any(at not in mapped and tuple(tvfs[at+9:at+11]) == LOCATION
           for at in range(co, co+cs, 25)):
        raise ValueError('compact archive would alias an omitted resource')

    destination = out/'game'
    data = destination/'Update/RomFS/data'
    data.mkdir(parents=True, exist_ok=False)
    archive = data/casc.archive_filename(*LOCATION)
    handles, relocated, ledger = {}, {}, {}
    source_naming = Counter()
    original_tvfs = bytes(tvfs)
    try:
        with archive.open('xb') as stream:
            for number, at in enumerate(sorted(mapped)):
                key = bytes(tvfs[at:at+16])
                length = struct.unpack_from('>I', tvfs, at+16)[0]
                filenames = list(dict.fromkeys((casc.archive_filename(key[9], key[10]),
                                               f'{key[9]:02d}-{key[10]:08d}.data')))
                raw = None
                for candidate in (root/'data'/name for root in roots for name in filenames):
                    if not candidate.is_file():
                        continue
                    if candidate not in handles:
                        handles[candidate] = candidate.open('rb')
                    handle = handles[candidate]
                    handle.seek(int.from_bytes(key[11:16], 'big'))
                    content = handle.read(length)
                    if len(content) == length and casc.ekey_for(content, key[9], key[10], int.from_bytes(key[11:16], 'big')) == key:
                        raw = content
                        source_naming[candidate.name] += 1
                        break
                if raw is None:
                    raise ValueError('retained encoded resource identity mismatch: '+mapped[at][0])
                offset = stream.tell()
                stream.write(raw)
                new_key = casc.ekey_for(raw, *LOCATION, offset)
                tvfs[at:at+16] = new_key
                relocated[at] = new_key
                ledger[at] = (offset, length, hashlib.sha256(raw).hexdigest())
                if number and number % 10000 == 0:
                    print('Repacked', number, 'entries;', stream.tell(), 'bytes', flush=True)

            def append(content):
                encoded = casc.blte_encode(content)
                key = casc.ekey_for(encoded, *LOCATION, stream.tell())
                stream.write(encoded)
                return key, len(content), len(encoded)

            vfs_key, vfs_size, vfs_encoded = append(bytes(tvfs))
            old_root_key = bytes.fromhex(config['vfs-root'].split()[1])
            root_size, root_encoded = map(int, config['vfs-root-size'].split())
            root = bytearray(casc.read_data_file(roots[0]/'data', old_root_key, root_encoded))
            old_vfs_key = bytes.fromhex(config['vfs-1'].split()[1])
            if len(root) != root_size or root.count(old_vfs_key) != 1:
                raise ValueError('unreviewed CASC root reference')
            at = root.index(old_vfs_key)
            root[at:at+16] = vfs_key
            casc.set_cft_sizes(root, at, vfs_encoded, vfs_size)
            root_key, root_size, root_encoded = append(bytes(root))
    finally:
        for handle in handles.values():
            handle.close()

    lines = casc.replace_config(config_lines, 'vfs-1', hashlib.md5(tvfs).hexdigest()+' '+vfs_key.hex())
    lines = casc.replace_config(lines, 'vfs-1-size', f'{vfs_size} {vfs_encoded}')
    for key in ('vfs-root', 'vfs-2'):
        if config.get(key) != config['vfs-root']:
            raise ValueError('unreviewed CASC secondary root')
        lines = casc.replace_config(lines, key, hashlib.md5(root).hexdigest()+' '+root_key.hex())
        lines = casc.replace_config(lines, key+'-size', f'{root_size} {root_encoded}')
    (data/'.build.config').write_text(''.join(lines))
    # Keep extraction metadata useful while giving every retained row its new address.
    with (data/'build_manifest.psv').open('x') as stream:
        for line in (roots[-1]/'data/build_manifest.psv').read_text().splitlines():
            if line.startswith('#') or line.startswith('FileId!'):
                stream.write(line+'\n')
        for name, at in sorted(selected.items()):
            row = rows[name].copy()
            row[2], row[3], row[6] = str(LOCATION[0]), str(LOCATION[1]), relocated[at].hex()
            row[7] = str(struct.unpack_from('>I', tvfs, at+16)[0])
            stream.write('|'.join(row)+'\n')

    # Verify all retained encoded payloads after writing, not only their index metadata.
    assert casc.load_vfs1(data.parent) == tvfs
    allowed_changes = set()
    with archive.open('rb') as stream:
        for at, (offset, length, expected) in ledger.items():
            stream.seek(offset)
            if hashlib.sha256(stream.read(length)).hexdigest() != expected:
                raise ValueError('compact payload round trip failed')
            allowed_changes.update(range(at, at+16))
    if any(a != b and i not in allowed_changes for i, (a, b) in enumerate(zip(original_tvfs, tvfs))):
        raise ValueError('unexpected CASC table change')
    for directory in ('classic-native-v6', 'classic-shaders'):
        shutil.copytree(game/directory, destination/directory)
    (destination/'classic-frontend').mkdir()
    shutil.copyfile(game/'classic-frontend/complete.json', destination/'classic-frontend/complete.json')
    for name in ('device-profile.json', 'classic-defaults.json', *(['owned-content.txt','owned-content.json'] if game_build=='94012' else [])):
        shutil.copyfile(game/name, destination/name)
    write_json(destination/'classic-bundle.json', compact_descriptor(device, game_build))
    resources = {str(path.relative_to(destination)): dict(size=path.stat().st_size, sha256=sha256(path))
                 for path in sorted(data.iterdir())}
    report = dict(schema=1, policy=POLICY, complete=True, game_build=game_build,
        device_profile_sha256=device['profile_sha256'], languages=['zhcn', 'enus'], video_playback=False,
        retained_encoded_bytes_unchanged=True, retained_entries_verified=len(ledger),
        retained_logical_paths=len(selected), omitted_encoded_bytes=dict(omitted),
        source_archive_reads=dict(source_naming),
        requires_original_archives=False, unavailable_original_index_entries_retained=True,
        source_manifest_sha256=sha256(game/'game-manifest.json'), resources=resources,
        resource_bytes=sum(row['size'] for row in resources.values()),
        runtime_rendering_verified=False, certifies_2gb=False)
    write_json(destination/'compact-classic.json', report)
    files = {str(path.relative_to(destination)): dict(size=path.stat().st_size, sha256=sha256(path))
             for path in sorted(destination.rglob('*')) if path.is_file()}
    write_json(destination/'game-manifest.json', dict(game_build=game_build, files=files))
    for name in ('install_game.py', 'classic_bundle.py', 'device_profiles.py', 'compact_classic.py'):
        shutil.copyfile(Path(__file__).with_name(name), out/name)
    shutil.copytree(source/'devices', out/'devices')
    shutil.copyfile(source/'install-classic.sh', out/'install-classic.sh')
    verify_compact(destination, dict(files=files, game_build=game_build), device)
    report['complete_bundle_bytes'] = sum(path.stat().st_size for path in out.rglob('*') if path.is_file())
    write_json(out/'preparation.json', report)
    print(json.dumps(report, ensure_ascii=False, indent=2), flush=True)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Build a standalone flat-menu Chinese classic bundle.')
    parser.add_argument('--source', type=Path, required=True, help='verified full ready directory')
    parser.add_argument('--out', type=Path, required=True, help='new private output directory')
    args = parser.parse_args()
    compact(args.source, args.out)
