#!/usr/bin/env python3
# INPUT: Completed private C1+O bank and explicit host validator/mksquashfs tools.
# OUTPUT: Private classic display bank with one reviewed RGB filter changed.
# POS: Offline quality stage; originals, shader identity, metadata and alpha survive.
"""Prepare mild classic display sharpening without resizing character assets."""
import argparse,hashlib,json,math,shutil,struct,subprocess
from pathlib import Path
from optimize_shaders import interface
from prepare_handheld import shader_index_keys
from spirv_classic_display import sharpen

KEY='e44ae4b6e36e3424'
POLICY='classic-display-scene-rgb-unsharp-v2'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('prepared','out','validator','mksquashfs'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--amount',type=float,default=.4,help='mild RGB sharpening 0..0.5, excluding zero; default 0.4')
    a=p.parse_args();source=a.prepared.resolve();out=a.out.resolve()
    if out.exists() or source in out.parents or out in source.parents or not math.isfinite(a.amount) or not 0<a.amount<=.5:
        p.error('new separate output and amount >0..0.5 required')
    proof=json.loads((source/'complete.json').read_text())
    if not proof.get('complete') or proof.get('classic_display_policy'):p.error('complete unfiltered source bank required')
    report=proof['shader_report'];index=source/'shaders/shader_index.bin'
    if digest(index)!=report['index_sha256']:p.error('original shader index provenance mismatch')
    keys=shader_index_keys(index.read_bytes());records={r['key']:r for r in report['records']}
    if keys.keys()!=records.keys() or keys.get(KEY)!=1:p.error('reviewed classic fragment missing from inventory')
    out.mkdir(parents=True,mode=0o700);bank=out/'shaders';bank.mkdir();shutil.copyfile(index,bank/index.name)
    rows=[]
    for key in sorted(keys):
        record=records[key];src=source/'shaders'/(key+'.spv');meta=source/'shaders'/(key+'.vkmeta')
        if src.is_symlink() or meta.is_symlink() or digest(src)!=record['spirv_sha256'] or digest(meta)!=record['metadata_sha256']:
            raise ValueError('private source provenance mismatch: '+key)
        target=bank/src.name
        if key==KEY:
            raw=src.read_bytes();metadata=meta.read_bytes()
            if struct.unpack_from('<I',metadata,44)[0]:raise ValueError('requires raw-file metadata')
            variant=sharpen(raw,metadata,a.amount);before,after=interface(raw),interface(variant)
            if before[:2]!=after[:2] or not after[2]<=before[2]|{50}:raise ValueError('unexpected filter interface change')
            target.write_bytes(variant)
            subprocess.run([str(a.validator.resolve()),'--target-env','vulkan1.1',str(target)],check=True)
            record=dict(record,source_spirv_sha256=record['spirv_sha256'],spirv_sha256=digest(target),
                        optimized_bytes=len(variant),classic_display_policy=POLICY,classic_display_amount=a.amount)
        else:shutil.copyfile(src,target)
        shutil.copyfile(meta,bank/meta.name);rows.append(record)
    image=out/'shaders.squashfs'
    with (out/'squashfs.log').open('w') as log:
        subprocess.run([str(a.mksquashfs.resolve()),str(bank),str(image),'-noappend','-comp','zstd',
            '-Xcompression-level','3','-processors','2','-no-progress','-mkfs-time','0','-all-root','-no-xattrs'],check=True,stdout=log)
    updated=dict(report,records=rows,raw_spirv_bytes=sum((bank/(r['key']+'.spv')).stat().st_size for r in rows),shader_code_changed=True)
    result=dict(proof,source_complete_sha256=digest(source/'complete.json'),shader_report=updated,
        classic_display_policy=POLICY,classic_display_amount=a.amount,classic_display_key=KEY,
        optimized_spirv_bytes=updated['raw_spirv_bytes'],
        image_bytes=image.stat().st_size,image_sha256=digest(image),original_code_identity_unchanged=True,
        metadata_unchanged=True,alpha_unchanged=True,source_characters_resized=False,
        tool_source_sha256={q.name:digest(q) for q in (Path(__file__),Path(__file__).with_name('spirv_classic_display.py'))},
        validator_sha256=digest(a.validator),mksquashfs_sha256=digest(a.mksquashfs),
        runtime_rendering_verified=False,target_fps_verified=False)
    (out/'complete.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('complete','classic_display_policy','classic_display_amount','image_sha256','alpha_unchanged')}))
if __name__=='__main__':main()
