"""Explicit low-quality albedo lighting for reviewed model/foliage fragments.

Preserve original UV computation and alpha stores. Replace only RGB output
stores dominated by the original slot-10 texture sample; keep fallback paths.
This changes lighting, not an equivalence optimization. Decline side effects,
depth output, ambiguous samples or unfamiliar pointer/control shapes. Original
discard/helper-invocation operations and their control dependencies are retained.
"""
import struct


def albedo_lighting(raw, metadata):
    if len(raw) % 4 or len(metadata) < 48:
        raise ValueError('truncated shader/metadata')
    words = list(struct.unpack(f'<{len(raw)//4}I', raw))
    if len(words) < 5 or words[0] != 0x07230203:
        raise ValueError('invalid SPIR-V')
    h = struct.unpack_from('<12I', metadata)
    if (h[:3] != (0x4d4b5644, 1, 1) or h[3] > 64 or h[4] > 32 or h[6] > 32 or
            len(metadata) != 48+h[3]*32+h[4]*8+h[6]*4+h[5]):
        raise ValueError('fragment metadata required')
    bindings = [struct.unpack_from('<8i', metadata, 48+i*32) for i in range(h[3])]
    if any(b[3] == 6 or b[2] == 5 or b[3] in (3, 9) and b[7] for b in bindings):
        raise ValueError('fragment has writable resources')
    albedos = [b for b in bindings if b[2:5] == (1, 4, 10) and b[6:] == (1, 3)]
    if len(albedos) != 1:
        raise ValueError('one original 2D slot-10 albedo binding required')
    inst, at = [], 5
    while at < len(words):
        n = words[at] >> 16
        if not n or at+n > len(words):
            raise ValueError('invalid instruction bounds')
        inst.append(words[at:at+n]); at += n
    if ([x[1] for x in inst if x[0] & 65535 == 15] != [4] or
            sum(x[0] & 65535 == 54 for x in inst) != 1 or
            any(x[0] & 65535 in (57, 63, 64, 99, 318, 319, 5614, 5615, 6035) or
                227 <= x[0] & 65535 <= 242 for x in inst)):
        raise ValueError('nonfragment entry, calls or external writes are outside the quality policy')
    types = {x[1]: x for x in inst if 19 <= x[0] & 65535 <= 39}
    defs = {x[2]: x for x in inst if x[0] & 65535 in (43, 59, 61, 65, 66, 83, 86)}
    decorations = {}
    for x in inst:
        if x[0] & 65535 == 71 and len(x) == 4:
            decorations[x[1], x[2]] = x[3]
    variables = {x[2]: x for x in inst if x[0] & 65535 == 59}
    textures = [v for v, x in variables.items() if x[3] == 0 and
                (decorations.get((v, 34)), decorations.get((v, 33))) == albedos[0][:2]]
    outputs = [v for v, x in variables.items() if x[3] == 3]
    if len(textures) != 1 or len(outputs) != 1 or decorations.get((outputs[0], 30)) != 0:
        raise ValueError('one albedo descriptor and one location-0 color output required')
    output = outputs[0]
    pointer = types[variables[output][1]]
    vector = types[pointer[3]]
    if pointer[2] != 3 or vector[0] & 65535 != 23 or vector[3] != 4:
        raise ValueError('float4 output required')
    scalar = types[vector[2]]
    if scalar[0] & 65535 != 22 or scalar[2] != 32:
        raise ValueError('32-bit float color required')

    def root(rid, depth=0):
        if depth > 16 or rid not in defs:
            raise ValueError('unreviewed store pointer')
        x = defs[rid]
        if x[0] & 65535 == 59:
            return rid
        if x[0] & 65535 not in (65, 66, 83):
            raise ValueError('unreviewed store pointer')
        return root(x[3], depth+1)

    blocks, samples, stores = {}, [], []
    current = None
    for i, x in enumerate(inst):
        op = x[0] & 65535
        if op == 248:
            current = x[1]; blocks[current] = []
        if current is not None:
            blocks[current].append(x)
        if op == 61 and root(x[3]) == output:
            raise ValueError('color output is read by later shader logic')
        if op in (87, 88):
            image = defs.get(x[3])
            if image and image[0] & 65535 == 61 and image[3] == textures[0]:
                if x[1] != pointer[3]:
                    raise ValueError('albedo sample/output type mismatch')
                samples.append((i, current, x[2]))
        if op == 62:
            v = root(x[1])
            if variables[v][3] not in (3, 6, 7):
                raise ValueError('store has external effects')
            if v == output:
                p = defs[x[1]]
                if p[0] & 65535 != 65 or len(p) != 5 or p[3] != output or len(x) != 3:
                    raise ValueError('scalar direct color stores required')
                c = defs.get(p[4])
                if not c or c[0] & 65535 != 43 or len(c) != 4 or c[3] > 3:
                    raise ValueError('static output channel required')
                stores.append((i, current, c[3]))
    if len(samples) != 1 or not stores:
        raise ValueError('unambiguous albedo sample and color stores required')
    predecessors = {b: set() for b in blocks}
    for b, rows in blocks.items():
        for x in rows:
            op = x[0] & 65535
            targets = x[1:2] if op == 249 else x[2:4] if op == 250 else [x[2], *x[4::2]] if op == 251 else []
            for target in targets:
                if target not in predecessors:
                    raise ValueError('branch outside function')
                predecessors[target].add(b)
    entry = next(iter(blocks))
    # Reject unreachable blocks: no vacuous dominance can admit an undefined sample.
    reachable, pending = {entry}, [entry]
    while pending:
        b = pending.pop()
        for successor, parents in predecessors.items():
            if b in parents and successor not in reachable:
                reachable.add(successor); pending.append(successor)
    if reachable != set(blocks):
        raise ValueError('unreachable control flow needs prior cleanup')
    dom = {b: {entry} if b == entry else set(blocks) for b in blocks}
    while True:
        updated = {b: {entry} if b == entry else {b} | set.intersection(*(dom[p] for p in predecessors[b]))
                   for b in blocks}
        if updated == dom:
            break
        dom = updated
    sample_i, sample_block, sample = samples[0]
    rewrite = {i: channel for i, b, channel in stores if channel < 3 and sample_block in dom[b] and
               (sample_block != b or sample_i < i)}
    if set(rewrite.values()) != {0, 1, 2}:
        raise ValueError('albedo does not dominate a complete RGB output path')
    result, next_id = [], words[3]
    for i, x in enumerate(inst):
        if i in rewrite:
            result.append([(5 << 16) | 81, vector[2], next_id, sample, rewrite[i]])
            x = [x[0], x[1], next_id]
            next_id += 1
        result.append(x)
    header = words[:5]; header[3] = next_id
    flat = header + [v for x in result for v in x]
    return struct.pack(f'<{len(flat)}I', *flat), dict(rgb_stores_replaced=len(rewrite),
        alpha_stores_preserved=sum(channel == 3 for _, _, channel in stores),
        fallback_rgb_stores_preserved=sum(channel < 3 for _, _, channel in stores)-len(rewrite),
        discard_operations_preserved=sum(x[0] & 65535 in (252, 4416, 5380) for x in inst),
        original_albedo_descriptor=list(albedos[0][:2]), original_uv_and_alpha_preserved=True)
