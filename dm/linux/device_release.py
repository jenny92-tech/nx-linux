#!/usr/bin/env python3
# INPUT: A reviewed device ID, built ARM64 runtime and the user's local game inputs.
# OUTPUT: Separate public software and private prepared game folders for that device.
# POS: Desktop release entry; preparation success never certifies hardware gameplay.
"""Generate one model directory; private game content remains on this computer."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from device_profiles import DEVICE_IDS, install_paths, load_device
from package_runtime import package, runtime_files
from package_preparer import preparer_files
from prepare_classic import TOOL_NAMES

HERE=Path(__file__).resolve().parent


def release_paths(out,device_id):
    load_device(device_id)
    root=Path(out).expanduser().resolve()/device_id
    return dict(root=root,public=root/'public',private=root/'private',
                preparation=root/'private'/'prepared',software=root/'public'/'d2r-linux.zip')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--device',choices=DEVICE_IDS,required=True)
    p.add_argument('--resources',choices=('compact','full'),default='compact')
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--build-dir',type=Path,required=True)
    p.add_argument('--wayland-wsi',type=Path,help='audited optional ARM64 WSI bundle, including its complete notices')
    inputs=p.add_mutually_exclusive_group(required=True)
    inputs.add_argument('--input',type=Path)
    inputs.add_argument('--extracted',type=Path)
    p.add_argument('--reuse-base',type=Path)
    p.add_argument('--tools-bin',type=Path)
    for name in TOOL_NAMES:p.add_argument('--'+name,type=Path)
    p.add_argument('--jobs',type=int,default=4)
    p.add_argument('--resume',action='store_true')
    p.add_argument('--check',action='store_true',help='verify the plan without writing files')
    a=p.parse_args();profile=load_device(a.device);paths=release_paths(a.out,a.device)
    build=a.build_dir.expanduser().resolve()
    runtime_files(build,a.wayland_wsi);preparer_files()
    binary=(build/'d2r-linux-novideo').read_bytes()
    if not all(v in binary for v in (b'DM_LINUX_STATIC_MENU',b'DM_LINUX_POOL_WINDOWS',
            b'DM_LINUX_COMPACT_ARRAY_IMAGES',b'DM_LINUX_INDEXED_SPRITES',
            b'DM_RENDER_SLOTS',b'DM_LINUX_COMPACT_2D_IMAGES',b'DM_TEMP_CACHE_IDLE_FRAMES')):
        p.error('device release requires the complete current runtime')
    command=[sys.executable,str(HERE/'prepare_classic.py'),'--device',a.device,
             '--resources',a.resources,'--out',str(paths['preparation']),'--jobs',str(a.jobs)]
    for name in ('input','extracted','reuse_base','tools_bin',*TOOL_NAMES):
        value=getattr(a,name)
        if value is not None:command+=['--'+name.replace('_','-'),str(value)]
    if a.resume:command+=['--resume']
    if a.check:command+=['--check']
    if paths['root'].exists() and not a.resume and not a.check:
        p.error('device output already exists; use a new directory or --resume')
    subprocess.run(command,check=True)
    plan=dict(schema=1,device_profile=profile,resource_bundle=a.resources,
        paths={k:str(v) for k,v in paths.items()},
        install_paths={k:str(v) for k,v in install_paths(profile).items()},
        preparation_complete=not a.check,gameplay_verified_on_this_memory_class=False,
        hardware_validation='pending',files_written=not a.check)
    if not a.check:
        paths['public'].mkdir(parents=True,exist_ok=True)
        # Existing archives are immutable; a resumed private pipeline does not
        # silently retain an older software package after sources have changed.
        if paths['software'].exists():
            p.error('software archive already exists; use a new release directory')
        package(build,paths['software'],wsi=a.wayland_wsi,with_preparer=True)
        (paths['root']/'release.json').write_text(json.dumps(plan,indent=2)+'\n')
        (paths['root']/'INSTALL.zh-CN.txt').write_text(
            '公共软件：public/d2r-linux.zip。私人游戏：private/prepared/ready/。\n'
            '解压公共软件，将 runtime/ 内容复制到 '+plan['install_paths']['runtime_root']+'。\n'
            '把整个 ready/ 复制到掌机；在该目录运行 bash install-classic.sh。\n'
            '已有角色和设置不覆盖。private/ 包含游戏派生文件，不可公开分发。\n'
            '目录名称只是目标配置；2 GB 机型仍需真实硬件游戏验证。\n')
    print(json.dumps(plan,indent=2))


if __name__=='__main__':
    try:main()
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        sys.exit('device release: '+str(error))
