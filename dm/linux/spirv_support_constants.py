"""Computer-only folding for the current Linux Vulkan support buffer contract.

The renderer zeroes its own 1392-byte UBO, sets 73 scales to exact float one,
and never modifies it. Do not use this contract with another backend or a
future renderer that changes these bytes. Game-provided uniforms are excluded.
"""
import struct

from spirv_c1_constants import bake_immutable_uniform

CONTRACT = 'linux-vulkan-support-v1-zero1392-scale73x1'


def bake_linux_support(raw, metadata, *, include_vertex=False):
    if len(metadata) < 48:
        raise ValueError('short shader metadata')
    h = struct.unpack_from('<12I', metadata)
    if h[:2] != (0x4d4b5644, 1) or h[3] > 64 or h[4] > 32 or h[6] > 32:
        raise ValueError('invalid shader metadata')
    start = 48 + h[3]*32 + h[4]*8 + h[6]*4
    if len(metadata) != start+h[5] or h[5] > 4096:
        raise ValueError('invalid shader metadata bounds')
    # Both graphics stages bind the same renderer-owned immutable backing.
    # Vertex folding is a separate offline experiment; compute stays unchanged.
    if h[2] != 1 and not (include_vertex and h[2] == 0):
        return raw, 0
    pairs = set()
    for i in range(h[3]):
        b = struct.unpack_from('<8i', metadata, 48+i*32)
        if b[:2] == (h[2], 0) and b[3] == 1 and b[6] == 1:
            pairs.add(b[:2])
    backing = bytearray(1392)
    for i in range(73):
        struct.pack_into('<I', backing, 192+i*16, 0x3f800000)
    return bake_immutable_uniform(raw, pairs, backing)
