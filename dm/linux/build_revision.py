#!/usr/bin/env python3
# INPUT: Maintained Linux, shared host/graphics and service source files.
# OUTPUT: SHA256 identity for the native runtime's source inputs.
# POS: Source identity shared by the production build and package gate.
import hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
PRIVATE_GENERATED={'dm_vault_tab.h','dm_hud_exp.h','dm_bright_levels.inc'}

def source_revision():
    files=[]
    for directory in ('dm/linux','dm/host','dm/gfx','dm/android'):
        for path in (ROOT/directory).rglob('*'):
            relative=path.relative_to(ROOT)
            if any(p in {'out','tests','bin','obj','__pycache__'} for p in relative.parts): continue
            if path.name in PRIVATE_GENERATED: continue
            if path.is_file() and (path.suffix in {'.c','.h','.inc','.S'} or path.name in {'Makefile','build.sh','build_revision.py','runtime_identity.py'}):
                if path.is_symlink(): raise ValueError('linked runtime source input')
                files.append(path)
    files.append(ROOT/'dm/third_party/bcdec/bcdec.h')
    digest=hashlib.sha256()
    for path in sorted(files):
        name=path.relative_to(ROOT).as_posix().encode();data=path.read_bytes()
        digest.update(len(name).to_bytes(4,'little'));digest.update(name)
        digest.update(len(data).to_bytes(8,'little'));digest.update(data)
    return digest.hexdigest()

if __name__=='__main__': print(source_revision())
