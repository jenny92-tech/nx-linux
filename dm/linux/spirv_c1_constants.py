"""Bake only statically addressed reads of the renderer-owned immutable C1 UBO.

Its 64 KiB backing is zero-initialized before the embedded constant prefix is
copied in shader_load_into. Dynamic indices, unusual types and volatile loads
retain their original instructions. All result IDs and resource ABI remain.
"""
import struct


def bake_c1(raw, metadata):
    h = struct.unpack_from('<12I', metadata)
    if h[:2] != (0x4d4b5644, 1) or h[3] > 64 or h[4] > 32 or h[6] > 32:
        raise ValueError('invalid shader metadata')
    start = 48 + h[3]*32 + h[4]*8 + h[6]*4
    if len(metadata) != start+h[5] or h[5] > 4096:
        raise ValueError('invalid C1 metadata bounds')
    c1 = metadata[start:] + bytes(65536-h[5])
    pairs = set()
    for i in range(h[3]):
        b = struct.unpack_from('<8i', metadata, 48+i*32)
        if b[2:5] == (6, 7, 1): pairs.add(b[:2])
    if not pairs:
        return raw, 0
    return bake_immutable_uniform(raw, pairs, c1)


def bake_immutable_uniform(raw, pairs, backing):
    """Fold static loads only; caller owns proof of the exact immutable bytes."""
    if not pairs:
        return raw, 0
    words = list(struct.unpack(f'<{len(raw)//4}I', raw))
    if words[0] != 0x07230203:
        raise ValueError('invalid SPIR-V')
    inst, at = [], 5
    while at < len(words):
        n = words[at] >> 16
        if not n or at+n > len(words): raise ValueError('invalid instruction')
        inst.append(words[at:at+n]); at += n
    types, defs, literals, bindings, sets, strides, offsets = {}, {}, {}, {}, {}, {}, {}
    variables = {}
    for x in inst:
        op = x[0] & 65535
        if 19 <= op <= 33: types[x[1]] = x
        if op in (43, 44, 46, 59, 65, 66, 83): defs[x[2]] = x
        if op == 43 and len(x) == 4: literals[x[2]] = x[3]
        if op == 59: variables[x[2]] = x
        if op == 71 and len(x) == 4:
            if x[2] == 33: bindings[x[1]] = x[3]
            if x[2] == 34: sets[x[1]] = x[3]
            if x[2] == 6: strides[x[1]] = x[3]
        if op == 72 and len(x) == 5 and x[3] == 35: offsets[x[1], x[2]] = x[4]
    c1vars = {v for v in variables if (sets.get(v), bindings.get(v)) in pairs}
    constants = {(x[1], x[3]): x[2] for x in inst if x[0] & 65535 == 43 and len(x) == 4}
    added, next_id = [], words[3]

    def pointer(pid, depth=0):
        if depth > 32: raise ValueError('pointer recursion')
        x = defs[pid]; op = x[0] & 65535
        if op == 59 and pid in c1vars:
            t = types[x[1]]
            if t[0] & 65535 != 32 or t[2] != 2 or x[3] != 2: raise ValueError('not a C1 uniform')
            return 0, t[3]
        if op not in (65, 66): raise ValueError('dynamic/other pointer')
        offset, typ = pointer(x[3], depth+1)
        for index in x[4:]:
            n = literals[index]; t = types[typ]; kind = t[0] & 65535
            if kind == 30:
                if n >= len(t)-2: raise ValueError('struct index')
                offset += offsets[typ, n]; typ = t[2+n]
            elif kind == 28:
                if n >= literals[t[3]]: raise ValueError('array index')
                offset += n*strides[typ]; typ = t[2]
            elif kind == 23:
                scalar = types[t[2]]
                if n >= t[3] or scalar[0] & 65535 not in (21, 22) or scalar[2] != 32:
                    raise ValueError('vector component')
                offset += n*4; typ = t[2]
            else: raise ValueError('unreviewed C1 layout')
        result = types[x[1]]
        if result[0] & 65535 != 32 or result[2] != 2 or result[3] != typ:
            raise ValueError('pointer result type')
        return offset, typ

    def constant(typ, offset):
        nonlocal next_id
        t = types[typ]; op = t[0] & 65535
        if op in (21, 22) and t[2] == 32:
            if offset < 0 or offset+4 > len(backing): raise ValueError('immutable UBO offset')
            bits = struct.unpack_from('<I', backing, offset)[0]; key = typ, bits
            if key not in constants:
                constants[key] = next_id; added.append([(4<<16)|43, typ, next_id, bits]); next_id += 1
            return constants[key]
        if op == 23 and 1 <= t[3] <= 4:
            components = [constant(t[2], offset+i*4) for i in range(t[3])]
            result = next_id; next_id += 1
            added.append([((3+len(components))<<16)|44, typ, result, *components]); return result
        raise ValueError('unreviewed C1 load type')

    result, folded = [], 0
    for x in inst:
        if x[0] & 65535 == 61 and len(x) == 4:
            try:
                offset, typ = pointer(x[3])
                if typ != x[1]: raise ValueError('C1 load type mismatch')
                value = constant(typ, offset)
            except (KeyError, ValueError):
                pass
            else:
                x = [(4<<16)|83, x[1], x[2], value]; folded += 1
        result.append(x)
    if not folded: return raw, 0
    first = next(i for i,x in enumerate(result) if x[0] & 65535 == 54)
    result[first:first] = added; words[3] = next_id
    flat = words[:5] + [v for x in result for v in x]
    return struct.pack(f'<{len(flat)}I', *flat), folded
