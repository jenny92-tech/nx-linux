#!/usr/bin/env python3
# INPUT: Built Linux service and an explicit allowlist of project-owned files.
# OUTPUT: Public runtime ZIP; no game data, keys, profiles, caches or diagnostics.
# POS: Release boundary separate from prepare_game.py's private device output.
"""Package only software. User-generated game directories are never traversed."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[2]
LINUX = ROOT / 'dm/linux'
WSI_FILES = ('VkLayer_window_system_integration.json', 'libVkLayer_window_system_integration.so',
    'LICENSE-Apache-2.0.txt', 'LICENSE-MIT.txt', 'LICENSE-Rudroid.txt', 'LICENSE-Vulkan-Headers.md',
    'LICENSE-WSI.txt', 'LICENSE-Wayland-Protocols.txt', 'NOTICE-Rudroid.txt',
    'THIRD-PARTY-NOTICES-Rudroid.txt', 'sources.json')


def runtime_files(build, wsi=None):
    result = {}
    binary = build / 'd2r-linux-novideo'
    from runtime_identity import production_identity
    production_identity(binary.read_bytes())
    result['runtime/d2r-linux-novideo'] = binary
    for name in ('run-bringup.sh', 'log-errors.py', 'portmaster-launch.sh.in', 'install_game.py', 'classic_bundle.py','device_profiles.py','compact_classic.py'):
        result['runtime/' + name] = LINUX / name
    from device_profiles import DEVICE_IDS
    for device_id in DEVICE_IDS:
        result['runtime/devices/'+device_id+'/profile.json']=LINUX/'devices'/device_id/'profile.json'
    result['README.md']=LINUX/'PACKAGED-INSTALL.zh-CN.md'
    result['AGENTS.md']=LINUX/'AGENTS.md'
    result['LICENSE'] = ROOT / 'LICENSE'
    result['licenses/bcdec-LICENSE.txt'] = ROOT / 'dm/third_party/bcdec/LICENSE'
    result['licenses/NVN-interface-MIT.txt'] = LINUX / 'licenses/NVN-interface-MIT.txt'
    result['licenses/deko3d-zlib.txt'] = LINUX / 'licenses/deko3d-zlib.txt'
    if wsi:
        manifest = json.loads((wsi / WSI_FILES[0]).read_text())
        if manifest['layer']['library_path'] != './libVkLayer_window_system_integration.so':
            raise ValueError('WSI manifest must use the bundled relative library')
        for name in WSI_FILES:
            path = wsi / name
            if not path.is_file() or path.is_symlink():
                raise ValueError('missing WSI library/provenance/notice: ' + name)
            result['runtime/wayland-wsi/' + name] = path
        header = (wsi / WSI_FILES[1]).read_bytes()[:20]
        if header[:6] != b'\x7fELF\x02\x01' or len(header) < 20 or struct.unpack_from('<H', header, 18)[0] != 183:
            raise ValueError('requires an ARM64 Linux WSI library')
    return result


def package(build, destination, wsi=None, with_preparer=False):
    files = runtime_files(build, wsi)
    if with_preparer:
        from package_preparer import preparer_files
        files.update(preparer_files())
    from runtime_identity import production_identity
    identity=production_identity((build/'d2r-linux-novideo').read_bytes())
    game_build=identity['game_build']
    receipt = {name: dict(size=path.stat().st_size,
        sha256=hashlib.sha256(path.read_bytes()).hexdigest()) for name, path in files.items()}
    with zipfile.ZipFile(destination, 'x', compression=zipfile.ZIP_DEFLATED) as out:
        for name, path in sorted(files.items()):
            out.write(path, name)
        out.writestr('runtime-manifest.json', json.dumps(dict(schema=1, game_build=game_build,
            contains_game_files=False, contains_keys=False, contains_generated_shaders=False,
            runtime_identity=identity,
            contains_optional_wsi_bundle=wsi is not None,
            contains_preparer_sources=with_preparer,
            files=receipt), indent=2) + '\n')
    return receipt


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', type=Path, default=LINUX / 'out')
    p.add_argument('--out', type=Path, required=True, help='new ZIP path')
    p.add_argument('--wayland-wsi', type=Path, help='explicit audited ARM64 WSI bundle; include all its original licenses/notices')
    p.add_argument('--with-preparer',action='store_true',help='include the fixed source-only desktop classic preparation bundle')
    args = p.parse_args()
    try:
        files = package(args.build_dir.resolve(), args.out.resolve(), args.wayland_wsi.resolve() if args.wayland_wsi else None,args.with_preparer)
    except (ValueError, OSError) as error:
        p.exit(1, str(error) + '\n')
    print(f'Public software only: {len(files)} allowlisted files; {args.out}')
