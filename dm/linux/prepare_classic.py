#!/usr/bin/env python3
# INPUT: Two user-owned NSPs plus prod.keys, and already installed host tools.
# OUTPUT: Private ready/game, optional resource gates/pool sizes and install metadata.
# POS: Computer-only classic distribution entry; never uploads keys or starts a game.
"""Prepare the tested RK3566 classic preset from a local input folder.

Install the host dependencies once. Put base.nsp, update.nsp and prod.keys in
--input, then run this entry; filenames of the two NSPs do not determine roles.
Only ready/ goes to the handheld. Its game content must never be redistributed.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import sys

from classic_bundle import DEFAULTS, PATHS, descriptor
from prepare_game import Pipeline, digest, tree_files, write_json, check_tree
from extract_local import verify_update
from device_profiles import DEVICE_IDS, install_paths, load_device

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
TOOL_NAMES = dict(hactool='hactool', astcenc='astcenc', dotnet='dotnet',
                  validator='spirv-val', optimizer='spirv-opt',
                  mksquashfs='mksquashfs', cc='cc')


def discover(directory):
    directory = directory.expanduser().resolve()
    if not directory.is_dir():
        raise ValueError('--input must be a directory')
    files=list(directory.rglob('*'))
    packages = sorted(p for p in files if p.suffix.lower() == '.nsp')
    if len(packages) != 2 or any(p.is_symlink() or not p.is_file() for p in packages):
        raise ValueError('input must contain exactly two regular NSP files: base and 1.0.28.0 update')
    key_files=[p for p in files if p.name.lower()=='prod.keys']
    title_files=[p for p in files if p.name.lower()=='title.keys']
    if len(key_files)!=1 or len(title_files)>1:
        raise ValueError('input requires exactly one prod.keys and at most one title.keys')
    keys, title = key_files[0],title_files[0] if title_files else None
    if keys.is_symlink() or not keys.is_file() or not keys.stat().st_size:
        raise ValueError('input requires your own nonempty regular prod.keys')
    if title is not None and (title.is_symlink() or not title.is_file()):
        raise ValueError('title.keys must be a regular file when provided')
    return packages, keys, title


def tools_for(args):
    result, missing = {}, []
    for name, command in TOOL_NAMES.items():
        value = getattr(args, name, None)
        candidate = Path(value).expanduser() if value else args.tools_bin/command if args.tools_bin else None
        found = str(candidate) if candidate else shutil.which(command)
        path = Path(found).resolve() if found else None
        if name=='hactool' and not args.input:
            continue
        if path is None or not path.is_file() or not os.access(path, os.X_OK):
            missing.append(command)
        else:
            result[name] = path
    if missing:
        raise ValueError('install host tools once, or set --tools-bin/individual paths: '+', '.join(missing))
    return result


def copy_private(source, target):
    """Link immutable intermediate assets when possible; original inputs are never linked."""
    source,target=Path(source),Path(target)
    if source.is_symlink() or not source.is_file():
        raise ValueError('nonregular prepared payload: '+str(source))
    try:
        os.link(source, target)
    except OSError:
        shutil.copy2(source, target)
    return str(target)


def assemble(base, native, shaders, frontend, out, device=None, game_build="90447", owned_content=None):
    for source in (base/'13-device/game/Update',base/'13-device/game/handheld-prepared-128',frontend/'overlay'):
        if source.is_symlink() or any(p.is_symlink() for p in source.rglob('*')):
            raise ValueError('linked prepared directories are not supported')
    game = out/'game'; game.mkdir(parents=True)
    shutil.copytree(base/'13-device/game/Update', game/'Update', copy_function=copy_private)
    shutil.copytree(base/'13-device/game/handheld-prepared-128', game/'handheld-prepared-128', copy_function=copy_private)
    module_dir = game/PATHS['modules']; module_dir.mkdir()
    report = json.loads((native/'classic-native.json').read_text())
    for name in [*report['output_files'], 'classic-native.json']:
        copy_private(native/name, module_dir/name)
    bank = game/Path(PATHS['shaders']).parent; bank.mkdir()
    for name in ('shaders.squashfs', 'complete.json'):
        copy_private(shaders/name, bank/name)
    menu = game/PATHS['frontend']; menu.mkdir()
    shutil.copytree(frontend/'overlay', menu/'overlay', copy_function=copy_private)
    copy_private(frontend/'complete.json', menu/'complete.json')
    write_json(game/'classic-defaults.json', DEFAULTS)
    write_json(game/'classic-bundle.json', descriptor(device, game_build))
    if device:write_json(game/'device-profile.json',device)
    if game_build == '94012':
        if owned_content is None:
            raise ValueError('94012 requires its verified owned-content metadata')
        write_json(game/'owned-content.json', owned_content)
        (game/'owned-content.txt').write_text('D2RAOC1 94012 db0507f36b84200ac05f75d76ed5026f 0100726014352000 0100726014353001 1 '+owned_content['package_sha256']+'\n')
    write_json(game/'game-manifest.json', dict(game_build=game_build, files=tree_files(game)))
    for name in ('install_game.py', 'classic_bundle.py', 'device_profiles.py', 'compact_classic.py'):
        shutil.copyfile(HERE/name, out/name)
    for device_id in DEVICE_IDS:
        target=out/'devices'/device_id;target.mkdir(parents=True)
        shutil.copyfile(HERE/'devices'/device_id/'profile.json',target/'profile.json')
    (out/'install-classic.sh').write_text('''#!/bin/bash
# Install only; encoding and shader translation were completed on the computer.
set -eu
root=$(cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$root/install_game.py" --classic-bundle --game-root "$root/game" "$@"
''')
    (out/'install-classic.sh').chmod(0o755)
    runtime_path=str(install_paths(device)['runtime_root']) if device else '/userdata/d2r-runtime'
    (out/'INSTALL.zh-CN.txt').write_text(
        '此目录包含您自己的游戏派生文件，不可作为公共软件包分发。\n'
        '先将公共 runtime/ 安装到掌机可执行分区 '+runtime_path+'。\n'
        '复制整个 ready/ 到掌机后执行 bash install-classic.sh。\n'
        '默认 LoongOS 路径；其他布局用 --runtime-root / --profile-root / --controllers / --entry 指定。\n'
        '安装器校验文件并生成 PortMaster 入口，不运行游戏。已有设置和角色不覆盖。\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    inputs=p.add_mutually_exclusive_group(required=True)
    inputs.add_argument('--input', type=Path)
    inputs.add_argument('--extracted',type=Path,help='advanced: your already merged, exact-build Update/ directory')
    p.add_argument('--game-build', choices=('90447', '94012'), default='90447')
    p.add_argument('--owned-content', type=Path, help='94012: computer-verified DLC metadata receipt')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--device',choices=DEVICE_IDS,help='reviewed model routing and fixed menu/memory preset; does not certify that memory class')
    p.add_argument('--resources', choices=('compact', 'full'), default='compact',
                   help='compact Chinese classic resources by default; full explicitly retains optional resources')
    p.add_argument('--tools-bin', type=Path)
    for name in TOOL_NAMES:
        p.add_argument('--'+name, type=Path)
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--menu-camera-fov',type=int,default=42,choices=range(38,43),
                   help='menu camera FOV; 39 enlarges the scene about 8%%, 42 keeps the original framing')
    p.add_argument('--classic-sharpness',type=float,default=.4,choices=(0.,.1,.2,.3,.4,.5),
                   help='classic display RGB sharpening; 0 keeps the original, default 0.4')
    p.add_argument('--buffer-pool-mib',type=int,
                   help='experimental original GPU buffer pool capacity, 64..400 MiB; default 400')
    p.add_argument('--texture-pool-mib',type=int,
                   help='experimental original texture storage pool capacity, 64..710 MiB; default 710')
    p.add_argument('--cpu-pool-mib',type=int,
                   help='experimental original CPU pool capacity, 64..250 MiB; default 250')
    terrain=p.add_mutually_exclusive_group()
    terrain.add_argument('--terrain-tile-side',type=int,choices=(64,),
                   help='experimental generated terrain resolution: 64-pixel cells / 1152-square atlases')
    terrain.add_argument('--terrain-auto',action='store_true',
                   help='experimental terrain resources follow actual internal render height on original rebuilds')
    resources=p.add_mutually_exclusive_group()
    resources.add_argument('--classic-resource-gate',action='store_true',
                   help='90447 classic-only experiment: block HD world resources before read/decode and omit 3D menu widgets')
    resources.add_argument('--classic-frontend-resources',action='store_true',
                   help='permit original frontend roots and restore classic resource gating on their original close callbacks')
    p.add_argument('--shader-nan-minmax',action='store_true',
                   help='experimental Maxwell NaN min/max/clamp semantics before SPIR-V optimization; default off')
    p.add_argument('--resume', action='store_true')
    p.add_argument('--reuse-base',type=Path,help='advanced: reuse a fully verified prepare_game.py output for the same extracted build')
    p.add_argument('--check', action='store_true', help='validate inputs/tools and show the plan; write nothing')
    a = p.parse_args()
    if a.game_build=='94012' and (a.resources!='compact' or not a.owned_content):
        p.error('94012 requires compact classic resources and --owned-content')
    owned_content=None
    if a.owned_content:
        owned_content=json.loads(a.owned_content.read_text())
        required=dict(title_id='0100726014353001', application_id='0100726014352000',
            game_build='94012', content_meta_type=130, version=0, index=1,
            metadata_nca_verified=True,
            package_sha256='0ddedb30a6aac83e613de61f72e0c182cb989d9f3e6018132b207fba8d60c849')
        if a.game_build!='94012' or any(owned_content.get(k)!=v for k,v in required.items()):
            p.error('owned-content metadata does not match the reviewed 94012 package')
    if a.resources=='compact':
        if a.device not in (None, 'rk3566-2gb'):
            p.error('the character-preview preset requires --resources full; use rk3566-2gb for compact classic resources')
        a.device='rk3566-2gb'
    device=load_device(a.device) if a.device else None
    bundle_descriptor=descriptor(device, a.game_build)
    if device:
        settings=device['settings']
        if a.classic_resource_gate or a.classic_frontend_resources or a.terrain_tile_side is not None or a.menu_camera_fov!=42 or a.shader_nan_minmax:
            p.error('device preset cannot be mixed with different resource, terrain, camera or NaN experiments')
        for name in ('buffer_pool_mib','texture_pool_mib','cpu_pool_mib'):
            if getattr(a,name) not in (None,settings[name]):p.error('capacity differs from the selected device preset')
            setattr(a,name,settings[name])
        if a.classic_sharpness!=settings['classic_sharpness']:
            p.error('sharpness differs from the selected device preset')
        a.terrain_auto=settings['terrain_auto'] if a.game_build=='90447' else False
        a.classic_frontend_resources=settings['classic_frontend_resources']
        a.classic_resource_gate=settings['classic_resource_gate']
    if not 1 <= a.jobs <= 8:
        p.error('--jobs must be 1..8')
    capacities={}
    native_capacity_args=[]
    for name,maximum in (('buffer',400),('texture',710),('cpu',250)):
        value=getattr(a,name+'_pool_mib')
        if value is not None and not 64<=value<=maximum:
            p.error('--'+name+'-pool-mib must be 64..'+str(maximum))
        capacities[name]=maximum if value is None else value
        if value is not None:
            native_capacity_args+=['--'+name+'-pool-mib',str(value)]
    native_terrain_args=[] if a.terrain_tile_side is None else ['--terrain-tile-side',str(a.terrain_tile_side)]
    if a.terrain_auto:native_terrain_args=['--terrain-auto']
    native_resource_args=['--classic-resource-gate'] if a.classic_resource_gate else ['--classic-frontend-resources'] if a.classic_frontend_resources else []
    terrain_tile_side='auto' if a.terrain_auto else 128 if a.terrain_tile_side is None else a.terrain_tile_side
    packages,keys,title=discover(a.input) if a.input else ([],None,None)
    out = a.out.expanduser().resolve(); folder = (a.input or a.extracted).expanduser().resolve()
    if out == folder or folder in out.parents or out in folder.parents:
        p.error('--out must be separate from the original input folder')
    if a.tools_bin:
        a.tools_bin = a.tools_bin.expanduser().resolve()
    tools = tools_for(a)
    original=verify_update(folder, a.game_build) if a.extracted else None
    if a.reuse_base and not a.extracted:
        p.error('--reuse-base requires --extracted; original NSO and resource identities are rechecked')
    if a.check:
        print(json.dumps(dict(ready=True, input_packages=len(packages),game_version_verified=bool(original),preset=bundle_descriptor['preset'],device_profile=device,resource_bundle=a.resources,menu_camera_fov_degrees=a.menu_camera_fov,classic_display_sharpness=a.classic_sharpness,
            pool_capacity_mib=capacities,pool_capacity_experiment=bool(native_capacity_args),
            terrain_tile_side=terrain_tile_side,terrain_resolution_experiment=bool(native_terrain_args),
            classic_resource_gate=a.classic_resource_gate,
            classic_frontend_resources=a.classic_frontend_resources,
            shader_nan_minmax=a.shader_nan_minmax,
            tools=list(tools), stages=['extract/validate', 'rewrite', 'textures', 'shaders',
            'classic native', *(['NaN semantics (experimental)'] if a.shader_nan_minmax else []),
            'optimized shaders', 'classic display filter', 'menu overlay',
            *(['Chinese/English classic resource repack'] if a.resources=='compact' else []), 'ready/game'],
            keys_uploaded=False, files_written=False))); return
    sources = {}
    for directory in ('dm/linux', 'dm/tools', 'dm/gfx/tools', 'dm/gfx/nvn', 'dm/gfx/tex', 'dm/third_party/ryujinx'):
        for path in (ROOT/directory).rglob('*'):
            rel = path.relative_to(ROOT)
            if (path.is_file() and not path.is_symlink() and
                    not any(n in ('out', 'bin', 'obj', '__pycache__') for n in rel.parts) and
                    (path.suffix in ('.py', '.c', '.h', '.inc', '.cs', '.csproj', '.props', '.targets', '.glsl', '.tsv')
                     or path.name.endswith('.S.in'))):
                sources[str(rel)] = digest(path)
    identity = dict(schema=1, game_build=a.game_build, owned_content=owned_content, preset=bundle_descriptor, device_profile=device, resource_bundle=a.resources, source_hashes=sources,
        inputs=original or [dict(size=f.stat().st_size, sha256=digest(f)) for f in packages],
        tools={name: digest(path) for name, path in tools.items()}, maximum_world_edge=128,
        menu_camera_fov_degrees=a.menu_camera_fov,classic_display_sharpness=a.classic_sharpness,
        pool_capacity_mib=capacities,pool_capacity_experiment=bool(native_capacity_args),
        terrain_tile_side=terrain_tile_side,terrain_resolution_experiment=bool(native_terrain_args),
        classic_resource_gate=a.classic_resource_gate,classic_frontend_resources=a.classic_frontend_resources,shader_nan_minmax=a.shader_nan_minmax)
    if a.reuse_base:
        a.reuse_base=a.reuse_base.expanduser().resolve()
        identity['reused_base_complete_sha256']=digest(a.reuse_base/'complete.json')
        identity['reused_base_pipeline_sha256']=digest(a.reuse_base/'pipeline.json')
    pipeline = Pipeline(out, a.resume, identity); py = sys.executable
    def run_stage(name, script, args):
        return pipeline.stage(name, lambda destination: pipeline.run(name,
            [py, HERE/script, *args, '--out', destination]))
    base_args = [*sum((['--package', x] for x in packages), []),
        '--astcenc', tools['astcenc'], '--dotnet', tools['dotnet'],
        '--validator', tools['validator'], '--mksquashfs', tools['mksquashfs'], '--cc', tools['cc'],
        '--max-side', '128', '--jobs', str(a.jobs), '--game-build', a.game_build]
    if packages:
        base_args+=['--keys',keys,'--hactool',tools['hactool']]
        if title:base_args+=['--title-keys',title]
    else:base_args+=['--extracted',folder]
    if a.reuse_base:
        def reuse(target):
            source=a.reuse_base
            proof=json.loads((source/'complete.json').read_text())
            inputs=json.loads((source/'pipeline.json').read_text())
            if not proof.get('complete') or proof.get('game_build')!=a.game_build or inputs.get('original')!=original:
                raise ValueError('cached base is incomplete or belongs to another original build')
            for stage in ('02-modules','03-tools','04-inventory','10-shader-cache','11-astc','12-handheld','13-device'):
                check_tree(source/stage,json.loads((source/stage/'stage.json').read_text())['files'])
            target.mkdir()
            for name in ('complete.json','pipeline.json'):
                shutil.copyfile(source/name,target/name)
            for name in ('02-modules','03-tools','04-inventory','13-device'):
                shutil.copytree(source/name,target/name,copy_function=copy_private)
        base=pipeline.stage('01-base',reuse)
    else:base = run_stage('01-base', 'prepare_game.py', base_args)
    native = run_stage('02-classic-native', 'prepare_classic_native.py',
        ['--game-build', a.game_build, '--modules', base/'02-modules', '--cc', tools['cc'],*native_capacity_args,*native_terrain_args,*native_resource_args])
    translator_args=[]
    if a.reuse_base:
        # A cached baseline can contain an older third-party shader library even
        # when Program.cs has the same embedded fingerprint. Never run that
        # cached translator for the current optimized policy.
        def build_translator(target):
            target.mkdir()
            pipeline.run('current-translator-build', [tools['dotnet'], 'publish',
                ROOT/'dm/gfx/tools/dmsh_xlate/dmsh_xlate.csproj', '-c', 'Release',
                '-o', target/'xlate', '--artifacts-path', target/'artifacts'])
        current=pipeline.stage('02-current-translator',build_translator)
        translator_args=['--translator',current/'xlate/dmsh_xlate']
    ast = run_stage('03-classic-ast', 'recompile_shaders.py', ['--prepared', base,
        '--policy', 'optimized', '--dotnet-root', tools['dotnet'].parent,
        '--validator', tools['validator'], '--mksquashfs', tools['mksquashfs'], '--jobs', str(a.jobs),
        *translator_args])
    semantic = ast
    if a.shader_nan_minmax:
        semantic = run_stage('03a-shader-nan-minmax', 'prepare_shader_nan_minmax.py',
            ['--prepared', ast, '--validator', tools['validator'],
             '--mksquashfs', tools['mksquashfs'], '--jobs', str(a.jobs)])
    optimized = run_stage('04-classic-c1-O', 'optimize_shaders.py', ['--prepared', semantic,
        '--bake-c1', '--optimizer', tools['optimizer'], '--validator', tools['validator'],
        '--mksquashfs', tools['mksquashfs'], '--jobs', str(a.jobs)])
    # The static menu cache removes recurring 3D work; retain original material
    # lighting instead of replacing it with the earlier albedo-only experiment.
    shaders = optimized
    if a.classic_sharpness:
        shaders = run_stage('05-classic-display', 'prepare_classic_display.py',
            ['--prepared', optimized, '--amount', str(a.classic_sharpness),
             '--validator', tools['validator'], '--mksquashfs', tools['mksquashfs']])
    frontend_options=(['--keep-character','--selected-character-only','--fit-handheld-modes'] if device['settings']['keep_character'] else ['--disable-player-capture','--fit-handheld-modes']) if device else [
        '--keep-character','--keep-background','--restore-menu-textures','--keep-menu-shadows','--fit-handheld-modes']
    frontend_textures=['--astc-work',(a.reuse_base or base)/'11-astc/work'] if not device or device['settings']['keep_character'] else []
    frontend = run_stage('06-classic-menu', 'prepare_classic_frontend.py',
        ['--game-build', a.game_build, '--romfs', base/'13-device/game/Update/RomFS', '--prepared', base/'13-device/game/handheld-prepared-128',
         *frontend_options,
         '--menu-camera-fov', str(a.menu_camera_fov), *frontend_textures,'--cc', tools['cc']])
    if a.resources=='compact':
        from compact_classic import compact
        full = pipeline.stage('07-full-ready', lambda target: assemble(base, native, shaders, frontend, target,device,a.game_build,owned_content))
        ready = pipeline.stage('ready', lambda target: compact(full, target))
    else:
        ready = pipeline.stage('ready', lambda target: assemble(base, native, shaders, frontend, target,device,a.game_build,owned_content))
    write_json(out/'complete.json', dict(complete=True, game_build=a.game_build, preset=bundle_descriptor['preset'],device_profile=device,resource_bundle=a.resources,
        menu_camera_fov_degrees=a.menu_camera_fov,classic_display_sharpness=a.classic_sharpness,
        pool_capacity_mib=capacities,pool_capacity_experiment=bool(native_capacity_args),
        terrain_tile_side=terrain_tile_side,terrain_resolution_experiment=bool(native_terrain_args),
        classic_resource_gate=a.classic_resource_gate,
        classic_frontend_resources=a.classic_frontend_resources,
        shader_nan_minmax=a.shader_nan_minmax,
        device_directory='ready', game_files_private=True, keys_in_device_package=False,
        game_manifest_sha256=digest(ready/'game/game-manifest.json'), gameplay_verified=False))
    print('Ready: copy only '+str(ready)+' to your handheld; never redistribute this private game output.')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as error:
        sys.exit('classic prepare: '+str(error))
