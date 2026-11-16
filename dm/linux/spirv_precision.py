"""Experimental fragment FP16 selection, keeping address/control paths at FP32.

This is a quality tradeoff, not an equivalence proof. Protect transitive inputs
of integer/boolean results, texture accesses, non-output stores and explicit
NoContraction operations. Only annotate remaining arithmetic; let SPIRV-Tools
insert the actual conversions and validate the complete module afterwards.
"""
import struct


def relax_color_arithmetic(raw):
    words = list(struct.unpack(f'<{len(raw)//4}I', raw))
    if len(words) < 5 or words[0] != 0x07230203:
        raise ValueError('invalid SPIR-V')
    inst, at = [], 5
    while at < len(words):
        n = words[at] >> 16
        if not n or at+n > len(words): raise ValueError('invalid instruction')
        inst.append(words[at:at+n]); at += n
    # Calls may pass pointers and hide data dependencies; this selector needs
    # the preceding -O inlining step and declines any remaining calls.
    if any(x[0] & 65535 == 57 for x in inst): return raw, 0
    types = {x[1]: x for x in inst if 19 <= x[0] & 65535 <= 39}
    defs, result_type = {}, {}
    for x in inst:
        op = x[0] & 65535
        if (op == 12 or op >= 41 and op not in (71, 72, 74, 75)) and len(x) >= 3 and x[1] in types:
            defs[x[2]], result_type[x[2]] = x, x[1]

    def floating(t):
        x = types.get(t, [0]); op = x[0] & 65535
        return op == 22 and x[2] == 32 or op in (23, 24) and floating(x[2])

    roots, precise, locations = set(), set(), set()
    for x in inst:
        if x[0] & 65535 == 71 and len(x) == 4 and x[2] == 30:
            locations.add(x[1])

    def output_color_pointer(rid):
        seen = set()
        while rid in defs and rid not in seen:
            seen.add(rid); x = defs[rid]; op = x[0] & 65535
            if op == 59:
                return x[3] == 3 and rid in locations
            if op not in (65, 66, 83): return False
            rid = x[3]
        return False

    for x in inst:
        op = x[0] & 65535
        # Keep control-flow joins and their producers at FP32. The tested
        # SPIRV-Tools half-conversion pass can otherwise leave OpPhi incoming
        # values with different widths. Final validation remains mandatory.
        if op == 245 and len(x) >= 5:
            roots.add(x[2]); roots.update(x[3::2])
        if op == 71 and len(x) == 3:
            if x[2] in (18, 42): precise.add(x[1])
        if op == 62:  # OpStore: preserve anything beyond an ordinary color output.
            if not output_color_pointer(x[1]):
                roots.update(x[1:])
        if 87 <= op <= 107 or 305 <= op <= 320:
            roots.update(x[3:] if len(x) >= 3 and x[1] in types else x[1:])
        if len(x) >= 3 and x[2] in result_type and result_type[x[2]] == x[1] and not floating(x[1]):
            roots.update(x[3:])
    roots.update(precise)
    protected, pending = set(), list(roots)
    while pending:
        rid = pending.pop()
        if rid in protected: continue
        protected.add(rid)
        if rid in defs: pending.extend(defs[rid][3:])
    arithmetic = {12, 127, 129, 131, 133, 136, 140, 141, 142, 143, 144, 145, 146, 147, 148}
    selected = {x[2] for x in inst if x[0] & 65535 in arithmetic and len(x) >= 3
                and floating(x[1]) and x[2] not in protected}
    # Remove preexisting relaxed decorations on protected values too; do not
    # let the conversion pass reintroduce low precision in a protected path.
    result = [x for x in inst if not (x[0] & 65535 == 71 and len(x) == 3 and x[2] == 0)]
    position = next(i for i,x in enumerate(result) if 19 <= x[0] & 65535 <= 39)
    result[position:position] = [[(3 << 16) | 71, rid, 0] for rid in sorted(selected)]
    flat = words[:5] + [v for x in result for v in x]
    return struct.pack(f'<{len(flat)}I', *flat), len(selected)
