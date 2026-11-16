# INPUT: Translated Maxwell SPIR-V before later authored shader effects.
# OUTPUT: Exact GLSL.std.450 min/max/clamp opcode substitutions and counts.
# POS: Offline compatibility pass; preserves IDs, operands, resources and size.
"""Use NaN-ignoring min/max/clamp for the translator's Maxwell operations.

FMin/FMax/FClamp do not specify the required single-NaN behavior. Their N
variants do. This pass validates the structure it uses, then changes only
the extended opcode words. It is not a full SPIR-V or semantic validator.
Based on the D2R Mobile NaN fix and dodola's bounded, atomic variant.
"""
import struct

POLICY = 'maxwell-nan-minmax-v1'
REPLACEMENTS = {37: (79, 'FMin', 7), 40: (80, 'FMax', 7), 43: (81, 'FClamp', 8)}


def rewrite_nan_minmax(raw):
    """Return new immutable bytes and counts; malformed input raises unchanged."""
    if len(raw) < 20 or len(raw) % 4:
        raise ValueError('invalid SPIR-V byte length')
    words = struct.unpack(f'<{len(raw)//4}I', raw)
    if words[0] != 0x07230203 or not 0 < words[3] <= 1000000:
        raise ValueError('invalid SPIR-V header/bound')
    imports, instructions = {}, []
    at = 5
    while at < len(words):
        size, code = words[at] >> 16, words[at] & 65535
        if not size or size > len(words) - at:
            raise ValueError('invalid SPIR-V instruction bounds')
        if code == 11:  # OpExtInstImport
            if size < 3 or not 0 < words[at+1] < words[3] or words[at+1] in imports:
                raise ValueError('invalid SPIR-V extension import')
            name = struct.pack(f'<{size-2}I', *words[at+2:at+size])
            if b'\0' not in name:
                raise ValueError('unterminated SPIR-V extension import')
            imports[words[at+1]] = name.split(b'\0', 1)[0]
        elif code == 12:  # OpExtInst
            if size < 5 or any(not 0 < words[at+i] < words[3] for i in (1, 2, 3)):
                raise ValueError('invalid SPIR-V extended instruction')
        instructions.append((at, size, code))
        at += size
    patches, counts = [], {name: 0 for _, name, _ in REPLACEMENTS.values()}
    for at, size, code in instructions:
        if code != 12 or imports.get(words[at+3]) != b'GLSL.std.450':
            continue
        replacement = REPLACEMENTS.get(words[at+4])
        if replacement is None:
            continue
        opcode, name, required = replacement
        if size != required:
            raise ValueError('invalid min/max/clamp operand count')
        patches.append((at+4, opcode))
        counts[name] += 1
    if not patches:
        return bytes(raw), counts
    updated = bytearray(raw)
    for at, opcode in patches:
        struct.pack_into('<I', updated, at*4, opcode)
    return bytes(updated), counts
