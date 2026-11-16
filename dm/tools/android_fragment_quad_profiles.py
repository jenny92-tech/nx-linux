"""Prove and fingerprint 94012's quad-local fragment shuffle variants.

The generated table admits exact modules only. Lane-dependent values may feed
only lane^1/2, the guest 32-lane clamp (lane&28)|3, its always-true validity
comparison, and the proved shuffle. A shuffle's block must already execute a
derivative, or its immutable interpolated input load is rematerialized in the
entry block, so the lowering introduces no new uniform-control requirement.
"""
from pathlib import Path
import argparse
import collections
import hashlib
import json
import struct


def instructions(data):
    if len(data) < 20 or len(data) % 4:
        raise ValueError('SPIR-V length')
    words = struct.unpack(f'<{len(data)//4}I', data)
    if words[0] != 0x07230203:
        raise ValueError('SPIR-V magic')
    result, at = [], 5
    while at < len(words):
        count = words[at] >> 16
        if not count or count > len(words)-at:
            raise ValueError('SPIR-V bounds')
        result.append(words[at:at+count])
        at += count
    return result


def prove(data):
    code = instructions(data)
    caps = {a[1] for a in code if a[0] & 65535 == 17}
    if 65 not in caps:
        return None
    if caps & set(range(61, 69)) != {65}:
        raise ValueError('additional subgroup capability')
    if sum(a[0] & 65535 == 54 for a in code) != 1:
        raise ValueError('multiple functions')
    if [a[1] for a in code if a[0] & 65535 == 15] != [4]:
        raise ValueError('not one fragment entry')
    defs, builtins = {}, {}
    typed = {43, 59, 61, 65, 83, 124, 178, 197, 198, 199, 345}
    for a in code:
        op = a[0] & 65535
        if op in (20, 21, 22, 23, 32):
            defs[a[1]] = a
        elif op in typed:
            defs[a[2]] = a
        if op == 71 and len(a) == 4 and a[2] == 11:
            if a[3] in builtins:
                raise ValueError('duplicate builtin')
            builtins[a[3]] = a[1]
    if not {15, 41} <= builtins.keys() or set(builtins) & set(range(36, 41)):
        raise ValueError('quad builtin shape')
    lane, coord = builtins[41], builtins[15]
    variable = defs[lane]
    if variable[0] & 65535 != 59 or variable[3] != 1:
        raise ValueError('lane input')
    pointer = defs[variable[1]]
    u32 = pointer[3]
    if pointer[2] != 1 or defs[u32][1:] != (u32, 32, 0):
        raise ValueError('lane uint32 type')
    coord_var = defs[coord]
    vec4 = defs[coord_var[1]][3]
    fp = defs[vec4][2]
    if defs[vec4][3] != 4 or defs[fp][1:] != (fp, 32):
        raise ValueError('fragment coordinate type')
    booleans = [a[1] for a in code if a[0] & 65535 == 20]
    if len(booleans) != 1:
        raise ValueError('bool type')
    constants = {a[2]: a[3] for a in code if a[0] & 65535 == 43 and len(a) == 4
                 and a[1] in defs and defs[a[1]][0] & 65535 == 21 and defs[a[1]][2] == 32}
    tags, allowed, axes, hoist = {}, set(), [], 0
    derivative = False
    for at, a in enumerate(code):
        op = a[0] & 65535
        if op == 248:
            derivative = False
        if 207 <= op <= 215:
            derivative = True
        if op == 61 and a[3] == lane:
            if len(a) != 4 or a[1] != u32:
                raise ValueError('lane load')
            tags[a[2]] = 'lane'
        elif op == 124 and a[3] in tags:
            if defs[a[1]][0] & 65535 != 21 or defs[a[1]][2] != 32:
                raise ValueError('lane cast type')
            tags[a[2]] = tags[a[3]]
            allowed.add((at, 3))
        elif op in (197, 198, 199) and any(v in tags for v in a[3:5]):
            if a[3] in tags and a[4] in constants:
                tag, value = tags[a[3]], constants[a[4]]
                allowed.add((at, 3))
            elif a[4] in tags and a[3] in constants:
                tag, value = tags[a[4]], constants[a[3]]
                allowed.add((at, 4))
            else:
                raise ValueError('lane binary operands')
            if op == 198 and tag == 'lane' and value in (1, 2):
                tags[a[2]] = ('index', value)
            elif op == 199 and tag == 'lane' and value == 28:
                tags[a[2]] = 'segment'
            elif op == 197 and tag == 'segment' and value == 3:
                tags[a[2]] = 'maximum'
            else:
                raise ValueError('lane binary shape')
        elif op == 178 and a[3] in tags and a[4] in tags:
            if tags[a[3]] not in (('index', 1), ('index', 2)) or tags[a[4]] != 'maximum':
                raise ValueError('guest lane validity')
            allowed.update(((at, 3), (at, 4)))
        elif 333 <= op <= 366:
            if op != 345 or len(a) != 6 or a[1] != fp or constants.get(a[3]) != 3:
                raise ValueError('shuffle shape/type/scope')
            if tags.get(a[5]) not in (('index', 1), ('index', 2)):
                raise ValueError('shuffle index')
            if not derivative:
                source = defs.get(a[4], ())
                if len(source) != 4 or source[0] & 65535 != 61:
                    raise ValueError('shuffle source cannot move to entry')
                access = defs.get(source[3], ())
                if len(access) < 5 or access[0] & 65535 != 65:
                    raise ValueError('shuffle input access')
                variable = defs.get(access[3], ())
                if len(variable) != 4 or variable[0] & 65535 != 59 or variable[3] != 1:
                    raise ValueError('shuffle immutable input')
                if any(index not in constants for index in access[4:]):
                    raise ValueError('shuffle constant input indices')
                hoist |= 1 << len(axes)
            axes.append(tags[a[5]][1]-1)
            allowed.add((at, 5))
    if not 1 <= len(axes) <= 3:
        raise ValueError('shuffle count')
    # Conservative all-word use audit: only integer-constant literal payloads
    # and debug names are exempt. This may reject extra modules, never admit
    # an unproved lane escape through an unknown instruction's operand.
    for at, a in enumerate(code):
        op = a[0] & 65535
        for j, value in enumerate(a[1:], 1):
            if value not in tags:
                continue
            if (at, j) in allowed or j == 2 and op in typed and a[2] in tags:
                continue
            if op == 5 and j == 1 or op == 43 and j >= 3:
                continue
            raise ValueError(f'unproved lane use opcode={op} operand={j}')
    fnv = 14695981039346656037
    for value in data:
        fnv = ((fnv ^ value) * 1099511628211) & ((1 << 64)-1)
    return dict(bytes=len(data), hash=fnv, fp=fp, u32=u32, boolean=booleans[0], vec4=vec4,
                coord=coord, lane=lane, axes=axes, hoist=hoist, sha256=hashlib.sha256(data).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shader-root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    profiles, rejected = [], []
    for meta in sorted(args.shader_root.glob('*.vkmeta')):
        if struct.unpack_from('<I', meta.read_bytes(), 8)[0] != 1:
            continue
        spv = meta.with_suffix('.spv')
        if not spv.is_file():
            continue
        try:
            proof = prove(spv.read_bytes())
        except ValueError as error:
            rejected.append(dict(key=meta.stem, reason=str(error)))
            continue
        if proof:
            profiles.append(dict(key=meta.stem, **proof))
    args.report.write_text(json.dumps(dict(profiles=profiles, rejected=rejected), indent=2)+'\n')
    rows = ['/* Generated by dm/tools/android_fragment_quad_profiles.py. Do not edit. */']
    for p in profiles:
        fields = ','.join(str(p[k]) for k in ('bytes', 'fp', 'u32', 'boolean', 'vec4', 'coord', 'lane'))
        rows.append('{"'+p['key']+'",UINT64_C(0x'+f"{p['hash']:016x}"+'),'+fields+','+str(len(p['axes']))+','+str(p['hoist'])+',{'+','.join(map(str,p['axes']))+'}},')
    args.out.write_text('\n'.join(rows)+'\n')
    print(json.dumps(dict(profiles=len(profiles), swaps=sum(len(p['axes']) for p in profiles),
                          axes=dict(collections.Counter(a for p in profiles for a in p['axes'])), rejected=rejected)))


if __name__ == '__main__':
    main()
