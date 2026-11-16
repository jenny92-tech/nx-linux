#!/usr/bin/env python3
# INPUT: Existing local experiment files and the identity of the hardware that produced them.
# OUTPUT: A model-scoped JSON inventory and a small Markdown navigation page.
# POS: Private archive indexer; does not move/delete evidence or certify another memory class.
"""Index retained experiments without copying large captures or changing their paths."""
import argparse
import json
from pathlib import Path
import re

from device_profiles import DEVICE_IDS, load_device


def index(root,device_id):
    profile=load_device(device_id);root=Path(root).resolve()
    if not root.is_dir():raise ValueError('missing experiment directory')
    groups={};unclassified=[]
    for path in sorted(root.rglob('*')):
        if path.is_symlink() or not path.is_file():continue
        relative=path.relative_to(root).as_posix()
        # Directory labels take precedence over hashed capture filenames.
        # Only authored C111char1/C111flat2 suffixes and single-letter phases
        # relax the separator rule; hexadecimal asset names are not run IDs.
        pattern=r'(?:^|[/_-])c(\d{1,3})(?=(?:(?:char|flat)\d+|[a-z])?(?:[/_.-]|$))'
        matches=re.findall(pattern,str(Path(relative).parent),re.I) or re.findall(pattern,Path(relative).name,re.I)
        row=dict(path=relative,bytes=path.stat().st_size)
        if matches:groups.setdefault('C'+max(matches,key=int),[]).append(row)
        else:unclassified.append(row)
    identity={k:profile[k] for k in ('id','model','soc','architecture','memory_mib')}
    return dict(schema=1,source_root=str(root),observed_device=identity,
        hardware_identity_supplied_by_operator=True,
        certifies_other_memory_classes=False,
        experiments={key:groups[key] for key in sorted(groups,key=lambda k:int(k[1:]))},
        unclassified=unclassified,evidence_moved=False,evidence_deleted=False)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True)
    p.add_argument('--observed-device',choices=DEVICE_IDS,required=True,
                   help='actual source hardware, never the desired deployment target')
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();root=a.root.expanduser().resolve();out=a.out.expanduser().resolve()
    if out==root or root in out.parents or out in root.parents:
        p.error('index output must be separate from the experiment source')
    report=index(root,a.observed_device);out=out/a.observed_device;out.mkdir(parents=True,exist_ok=True)
    (out/'experiments.json').write_text(json.dumps(report,indent=2)+'\n')
    rows=['# 历史实验索引','',report['observed_device']['model'],
          '', '此索引归属实际测试硬件；不证明同芯片的其他内存机型通过。原始记录保持原位。',
          '', '| 实验 | 文件数 | MiB |','|---|---:|---:|']
    for name,files in report['experiments'].items():
        rows.append('| '+name+' | '+str(len(files))+' | '+format(sum(x['bytes'] for x in files)/(1<<20),'.1f')+' |')
    rows+=['','未归类文件：'+str(len(report['unclassified']))+'；完整路径见 experiments.json。',
           '通过/失败、指标口径和限制以各实验原始回执及 PERFORMANCE.zh-CN.md 为准。','']
    (out/'experiments.md').write_text('\n'.join(rows))
    print(json.dumps(dict(device=a.observed_device,experiments=len(report['experiments']),
                         files=sum(len(v) for v in report['experiments'].values())+len(report['unclassified']),
                         out=str(out),source_modified=False)))


if __name__=='__main__':
    try:main()
    except (ValueError,OSError) as error:raise SystemExit('experiment index: '+str(error))
