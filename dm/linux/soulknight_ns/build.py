#!/usr/bin/env python3
# INPUT: Maintained runtime sources and the already installed cross-build image.
# OUTPUT: Source-matched stripped ARM64 Linux Soul Knight runtime and receipt.
# POS: Title build entry; does not acquire compilers, game files or drivers.
from pathlib import Path
import argparse,hashlib,json,struct,subprocess
from source_revision import source_revision
from runtime_identity import production_identity

root=Path(__file__).resolve().parents[3]
headers=root/'dm/linux/out/toolchain/vulkan-headers'
image='bogo-builder:dev'
expected='sha256:f539f5cddea8a2fca41644cc981a0bcf6abb3a968ffdf943f568bbc2fed72984'

def build(backend='vulkan'):
    if backend not in ('vulkan','gles'):raise ValueError('Unknown graphics backend')
    output=root/('dm/linux/out/soulknight_ns_gles' if backend=='gles' else 'dm/linux/out/soulknight_ns')
    name='soulknight-ns-gles' if backend=='gles' else 'soulknight-ns'
    identity=subprocess.check_output(['docker','image','inspect',image,'--format','{{.Id}}'],text=True).strip()
    if identity!=expected:raise ValueError('Existing builder identity differs')
    if not (headers/'vulkan/vulkan.h').is_file():raise ValueError('Existing Vulkan headers missing')
    before=source_revision();output.mkdir(parents=True,exist_ok=True)
    # Every object embeds the source revision flag. Rebuild after any source
    # identity change; a stale incremental link must never pass the gate.
    revision_file=output/'object-source-revision.txt'
    unchanged=revision_file.is_file() and revision_file.read_text().strip()==before
    command=['docker','run','--pull','never','--rm','--platform','linux/amd64','--network','none',
             '--read-only','--cap-drop','ALL','--security-opt','no-new-privileges','--cpus','2','--memory','4g',
             '--tmpfs','/tmp:rw,nosuid,size=128m',
             '--mount',f'type=bind,source={root},target=/src,readonly',
             '--mount',f'type=bind,source={output},target=/out',
             '--mount',f'type=bind,source={headers},target=/vk,readonly',
             '--workdir','/src/dm/linux',image,'make','--jobs=2',
             *([] if unchanged else ['-B']),'-f','soulknight_ns/Makefile','OUT=/out','CPPFLAGS=-I/vk',
             'CC=aarch64-linux-gnu-gcc',f'SOUL_RENDERER={backend}','/out/'+name]
    with (output/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
    if source_revision()!=before:raise ValueError('Runtime sources changed while building')
    binary=output/name;data=binary.read_bytes()
    if backend=='vulkan':validated=production_identity(data)
    else:
        validated={'profile':'Release','kind':'linux-native-gles-experiment','title_id':'0100a0800b83a000',
                   'game_build':'02242ae4e90dcf439210fe87f692de05','source_revision':before}
        marker=json.dumps(validated,separators=(',',':')).encode()+b'\0'
        if len(data)<64 or data[:7]!=b'\x7fELF\x02\x01\x01' or struct.unpack_from('<H',data,18)[0]!=183 or data.count(marker)!=1:
            raise ValueError('Experimental GLES ELF identity differs')
        try:production_identity(data)
        except ValueError:pass
        else:raise ValueError('GLES experiment passed the Vulkan production gate')
    if validated['source_revision']!=before:raise ValueError('Embedded source identity differs')
    report={'identity':validated,'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'builder_image_id':identity}
    revision_file.write_text(before+'\n')
    (output/'build-receipt.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--backend',choices=('vulkan','gles'),default='vulkan')
    build(p.parse_args().backend)
