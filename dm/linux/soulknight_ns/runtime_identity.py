#!/usr/bin/env python3
# INPUT: Complete candidate ELF and the current maintained source tree.
# OUTPUT: Statically validated Soul Knight NS Release identity.
# POS: Fail-closed packaging gate; never executes a candidate.
import json,re,struct
from source_revision import source_revision

TITLE='0100a0800b83a000'
BUILD='02242ae4e90dcf439210fe87f692de05'
MARKER=b'SOULKNIGHT-NS-GAME-BUILD='+BUILD.encode()+b'\0'

def production_identity(data):
    def reject():raise ValueError('Requires complete source-matched Soul Knight Release ARM64 runtime')
    if len(data)<64 or data[:7]!=b'\x7fELF\x02\x01\x01':reject()
    if struct.unpack_from('<H',data,16)[0] not in (2,3) or struct.unpack_from('<H',data,18)[0]!=183 or struct.unpack_from('<I',data,20)[0]!=1:reject()
    entry,phoff,shoff=struct.unpack_from('<QQQ',data,24)
    ehsize,phsize,phnum,shsize,shnum,strings_index=struct.unpack_from('<HHHHHH',data,52)
    if ehsize!=64 or phsize!=56 or not 1<=phnum<=256 or phoff+phsize*phnum>len(data):reject()
    loads=[];executable=False
    for i in range(phnum):
        kind,flags,offset,address,_,size,memsize,_=struct.unpack_from('<IIQQQQQQ',data,phoff+i*phsize)
        if offset+size>len(data) or size>memsize:reject()
        if kind==1:
            loads.append((offset,address,size))
            if flags&1 and address<=entry<address+size:executable=True
    if not executable or shsize!=64 or not 1<=shnum<=4096 or strings_index>=shnum or shoff+shsize*shnum>len(data):reject()
    sections=[struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*shsize) for i in range(shnum)]
    strings=sections[strings_index]
    if strings[1]!=3 or strings[4]+strings[5]>len(data):reject()
    names=data[strings[4]:strings[4]+strings[5]];identity=None
    def unique_object(pairs):
        result={}
        for key,value in pairs:
            if key in result:reject()
            result[key]=value
        return result
    for section in sections:
        if section[0]>=len(names):reject()
        end=names.find(b'\0',section[0])
        if end<0:reject()
        name=names[section[0]:end]
        if name.startswith((b'.debug',b'.zdebug')) or name==b'.symtab':reject()
        if section[1]!=8 and section[4]+section[5]>len(data):reject()
        if name==b'.soulknight_build':
            if identity is not None or section[1]!=1 or not section[2]&2 or not 1<=section[5]<=1024:reject()
            if not any(offset<=section[4] and section[4]+section[5]<=offset+size and
                       section[3]==address+section[4]-offset for offset,address,size in loads):reject()
            try:identity=json.loads(data[section[4]:section[4]+section[5]].rstrip(b'\0'),object_pairs_hook=unique_object)
            except (ValueError,UnicodeDecodeError):reject()
    if not isinstance(identity,dict) or set(identity)!={'profile','kind','title_id','game_build','source_revision'}:reject()
    if (identity['profile'],identity['kind'],identity['title_id'],identity['game_build'])!=('Release','linux-native-vulkan',TITLE,BUILD):reject()
    if not isinstance(identity['source_revision'],str) or not re.fullmatch('[0-9a-f]{64}',identity['source_revision']):reject()
    if identity['source_revision']!=source_revision():reject()
    if not any(MARKER in data[offset:offset+size] for offset,_,size in loads):reject()
    return identity
