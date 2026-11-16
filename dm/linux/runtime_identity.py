#!/usr/bin/env python3
# INPUT: A candidate ARM64 runtime ELF and its maintained source tree.
# OUTPUT: Validated production profile, runtime kind, game and source identity.
# POS: Fail-closed public packaging gate; never executes the candidate binary.
import json,re,struct
from build_revision import source_revision

def production_identity(data):
    def reject(): raise ValueError('requires a complete source-matched Release ARM64 runtime')
    if len(data)<64 or data[:6]!=b'\x7fELF\x02\x01': reject()
    if struct.unpack_from('<H',data,16)[0] not in (2,3) or struct.unpack_from('<H',data,18)[0]!=183: reject()
    entry,phoff,shoff=struct.unpack_from('<QQQ',data,24)
    phsize,phnum,shsize,shnum,strings_index=struct.unpack_from('<HHHHH',data,54)
    if phsize!=56 or not 1<=phnum<=256 or phoff+phsize*phnum>len(data): reject()
    executable=False
    for i in range(phnum):
        kind,flags,offset,address,_,size,memsize,_=struct.unpack_from('<IIQQQQQQ',data,phoff+i*phsize)
        if offset+size>len(data) or size>memsize: reject()
        if kind==1 and flags&1 and address<=entry<address+memsize: executable=True
    if not executable or shsize!=64 or not 1<=shnum<=4096 or strings_index>=shnum or shoff+shsize*shnum>len(data): reject()
    sections=[struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*shsize) for i in range(shnum)]
    strings=sections[strings_index]
    if strings[4]+strings[5]>len(data): reject()
    names=data[strings[4]:strings[4]+strings[5]];identity=None
    for section in sections:
        if section[0]>=len(names): reject()
        end=names.find(b'\0',section[0])
        if end<0: reject()
        name=names[section[0]:end]
        if name.startswith((b'.debug',b'.zdebug')) or name==b'.symtab': reject()
        if section[1]!=8 and section[4]+section[5]>len(data): reject()
        if name==b'.d2r_build':
            if identity is not None or section[5]>1024: reject()
            try: identity=json.loads(data[section[4]:section[4]+section[5]].rstrip(b'\0'))
            except (ValueError,UnicodeDecodeError): reject()
    if not isinstance(identity,dict) or set(identity)!={'profile','kind','game_build','source_revision'}: reject()
    if identity['profile']!='Release' or identity['kind']!='novideo' or identity['game_build'] not in ('90447','94012'): reject()
    if not isinstance(identity['source_revision'],str) or not re.fullmatch('[0-9a-f]{64}',identity['source_revision']): reject()
    if identity['source_revision']!=source_revision(): reject()
    marker=b'D2R-LINUX-GAME-BUILD='+identity['game_build'].encode()+b'\0'
    if marker not in data: reject()
    return identity
