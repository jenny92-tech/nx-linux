#!/usr/bin/env python3
# INPUT: Maintained Linux and shared runtime source cone.
# OUTPUT: Content identity shared by the Release compiler and package gate.
# POS: Source identity excludes generated outputs, fixtures and private tables.
import hashlib,os
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]
EXCLUDED={'out','tests','bin','obj','__pycache__'}
PRIVATE={'dm_vault_tab.h','dm_hud_exp.h','dm_bright_levels.inc'}
BUILD_FILES={'Makefile','build.sh','build.py','build_revision.py','source_revision.py','runtime_identity.py'}

def source_revision():
    files=[]
    for directory in ('dm/linux','dm/host','dm/gfx','dm/android'):
        for current,dirs,names in os.walk(ROOT/directory):
            dirs[:]=[name for name in dirs if name not in EXCLUDED]
            if any((Path(current)/name).is_symlink() for name in dirs):raise ValueError('Linked source directory')
            for name in names:
                path=Path(current)/name
                if name in PRIVATE or (path.suffix not in {'.c','.h','.inc','.S'} and name not in BUILD_FILES):continue
                if path.is_symlink():raise ValueError('Linked runtime source input')
                files.append(path)
    files.append(ROOT/'dm/third_party/bcdec/bcdec.h')
    digest=hashlib.sha256()
    for path in sorted(files):
        if path.is_symlink():raise ValueError('Linked third-party source input')
        name=path.relative_to(ROOT).as_posix().encode();data=path.read_bytes()
        digest.update(len(name).to_bytes(4,'little'));digest.update(name)
        digest.update(len(data).to_bytes(8,'little'));digest.update(data)
    return digest.hexdigest()

if __name__=='__main__':print(source_revision())
