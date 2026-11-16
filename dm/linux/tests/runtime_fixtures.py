# INPUT: Explicit synthetic ELF identity fields.
# OUTPUT: A complete, nonexecuted ARM64 ELF fixture with production sections.
# POS: Exercises package metadata validation without a device or proprietary file.
import json,struct
from build_revision import source_revision

def make_runtime_elf(profile='Release',kind='novideo',game_build='90447',revision=None,debug=False):
    identity=json.dumps(dict(profile=profile,kind=kind,game_build=game_build,source_revision=revision or source_revision())).encode()+b'\0'
    bodies=[('.text',b'\xc0\x03\x5f\xd6'),('.rodata',b'D2R-LINUX-GAME-BUILD='+game_build.encode()+b'\0'),('.d2r_build',identity)]
    if debug: bodies.append(('.debug_info',b'diagnostic'))
    names=b'\0'+b''.join(name.encode()+b'\0' for name,_ in bodies)+b'.shstrtab\0'
    data=bytearray(256);sections=[(0,0,0,0,0,0,0,0,0,0)]
    for name,body in bodies+[('.shstrtab',names)]:
        offset=len(data);data.extend(body)
        sections.append((names.index(name.encode()+b'\0'),3 if name=='.shstrtab' else 1,6 if name=='.text' else 2,0x400000+offset,offset,len(body),0,0,1,0))
    shoff=(len(data)+7)&~7;data.extend(b'\0'*(shoff-len(data)))
    for section in sections: data.extend(struct.pack('<IIQQQQIIQQ',*section))
    data[:16]=b'\x7fELF\x02\x01\x01'+b'\0'*9
    struct.pack_into('<HHIQQQIHHHHHH',data,16,2,183,1,0x400100,64,shoff,0,64,56,1,64,len(sections),len(sections)-1)
    struct.pack_into('<IIQQQQQQ',data,64,1,5,0,0x400000,0x400000,len(data),len(data),4096)
    return bytes(data)
