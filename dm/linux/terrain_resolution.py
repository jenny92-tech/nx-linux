# INPUT: Verified 90447 native-v6 modules with the reviewed 960x720 display.
# OUTPUT: Guarded offline hooks tying terrain storage to actual internal height.
# POS: Optional quality policy; original release/create path owns GPU lifetimes.
"""Emit bounded ARM64 terrain hooks without changing executable memory live."""
import struct

AUTO_CONTEXT = (
    (0x1541150, 'f50f1df8f44f01a9fd7b02a9fd830091'),  # original SP+8 is unused
    (0x153A91C, '68d700f00920815209f108b968d700f00910805209f908b968d700f00902805209f508b9'),
    (0x1541160, '55d70090a8f248b91f012471e0050054'),
    (0x15411A4, '08208152a8f208b948d700900910805209f908b948d7009009028052e00313aa09f508b9'),
    (0x15431FC, '002081d20020c1f221008052c0035fd6'),
    (0x14AF3A8, '609256f9e10a402968470294'),  # original rebuild passes internal w1/w2
    (0x14AEEC8, 'f90f1bf8f85f01a9f65702a9f44f03a9fd7b04a9fd0301912100231e88d48c52006868bc0108211e0acf9d522800251e17000a8b4100231e0008211e0900251eea0240b9f30300aa5f01086b81000054ea0640b95f01096be00000541f0100712419407ae10a0229e8260029e8260129e1000054'),
    (0xCA606C, '137880d2135ac0f2'),  # reviewed startup fallback: 960x720
    (0xCA60FC, '087880d2085ac0f2'),
)


def tile_for_height(height):
    if type(height) is not int or not 1 <= height <= 2160:
        raise ValueError('internal render height must be 1..2160')
    return max(32, min(128, ((height + 89) // 90) * 8))


def branch(pc, target):
    delta = target - pc
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError('terrain branch outside ARM64 immediate range')
    return 0x14000000 | ((delta // 4) & 0x3ffffff)


def adrp(reg, pc, target):
    pages = ((target & ~4095) - (pc & ~4095)) // 4096
    if not -(1 << 20) <= pages < (1 << 20):
        raise ValueError('terrain address outside ARM64 page range')
    return 0x90000000 | ((pages & 3) << 29) | (((pages >> 2) & 0x7ffff) << 5) | reg


# w9 = clamp(ceil(height/90)*8,32,128). Clamp first to prevent unsigned overflow.
# Original compute group side is eight; preserve all 18x18 atlas cells.
CALCULATE = (
    0x5280B408, 0x6B08013F, 0x1A898109,  # min(height,1440)
    0x11016529, 0x52800B48, 0x1AC80929, 0x531D7129,
    0x52800408, 0x6B08013F, 0x1A893109,
)


def prepare_auto(image):
    """All guards/space checks precede writes; source length and v6 trailer survive."""
    checks = [(at, bytes.fromhex(raw)) for at, raw in AUTO_CONTEXT]
    for at, expected in checks:
        if image[at:at+len(expected)] != expected:
            raise ValueError('unreviewed automatic terrain configuration at 0x%x' % at)
    if len(image) % 0x4000 or not 0x3078000 <= len(image) <= 64*1024**2:
        raise ValueError('automatic terrain requires prepared native-v6 modules')
    control = len(image) - 0x4000
    header = control - 256
    if (image[header:header+8] != b'D2RHD904' or
            struct.unpack_from('<II', image, header+8) != (6, 8) or
            struct.unpack_from('<I', image, header+24)[0] != control):
        raise ValueError('automatic terrain requires native-v6 trailer')
    source = struct.unpack_from('<Q', image, header+16)[0]
    start = ((source+15) & ~15) + 8*48
    if source < 0x3070000 or source >= header or start + 512 > header or any(image[start:header]):
        raise ValueError('automatic terrain requires unused validated executable padding')

    blocks = []
    def emit(words):
        at = start + sum(len(raw) for _, raw in blocks)
        blocks.append((at, struct.pack('<%dI' % len(words), *words)))
        return at

    # Initialization: current original renderer dimensions; only pre-initialization
    # null/zero uses the independently guarded 720-line display fallback. Save NZCV
    # and x16/x17; x8/x9 are the original overwritten configuration temporaries.
    at = start
    words = [0xA9BF47F0, 0xD53B4211, adrp(8, at+8, 0x301EAB0),
             0xF9400008 | ((0xAB0//8) << 10) | (8 << 5),
             0xB4000008 | (5 << 5), 0x91403908, 0xB94E7D09,
             0x34000009 | (2 << 5), branch(at+32, at+40), 0x52805A09]
    words += list(CALCULATE)
    words += [adrp(16, at+4*len(words), 0x30298F0), 0x0B090D28, 0x531F7908,
              0xB908F208, 0xB908FA09, 0x53037D29, 0xB908F609,
              0xAA1003E8, 0xD51B4211, 0xA8C147F0]
    words += [branch(at+4*len(words), 0x153A940)]
    initial = emit(words)

    # Rebuild: w2 is the newly computed internal height. The original frame has
    # an unused four-byte slot at SP+8. Keep the desired tile there across the
    # original GPU synchronization/destruction calls; do not publish it early.
    at = start + sum(len(raw) for _, raw in blocks)
    words = [0x2A0203E9, *CALCULATE, 0xB9000BE9, 0xB948F2A8,
             0x0B090D29, 0x531F7929, 0x6B09011F]
    words += [branch(at+4*len(words), 0x154116C)]
    comparison = emit(words)

    # Query the actual created atlas. Before first creation the reviewed display
    # is 720 lines, hence the same 1152 fallback used by initialization.
    at = start + sum(len(raw) for _, raw in blocks)
    words = [0xA9BF47F0, adrp(16, at+4, 0x30298F0),
             0xB9400000 | ((0x8F0//4) << 10) | (16 << 5),
             0x35000000 | (2 << 5), 0x52809000, 0xAA008000, 0xA8C147F0]
    words += [branch(at+4*len(words), 0x1543204)]
    query = emit(words)
    patches = [(0x153A91C, struct.pack('<9I', branch(0x153A91C, initial), *([0xD503201F]*8))),
               (0x1541168, struct.pack('<I', branch(0x1541168, comparison))),
               (0x15411A4, struct.pack('<9I', 0xB9400BE9, 0x0B090D28, 0x531F7908,
                                      0xB908F2A8, 0xB908FAA9, 0x53037D29,
                                      0xB908F6A9, 0xAA1303E0, 0xD503201F)),
               (0x15431FC, struct.pack('<2I', branch(0x15431FC, query), 0xD503201F))]
    for offset, raw in blocks + patches:
        image[offset:offset+len(raw)] = raw
    return dict(policy='terrain-internal-height-v1', automatic=True,
                startup_display=[960, 720], tile_grid=[18, 18], tile_range=[32, 128],
                rule='tile=clamp(ceil(internal_height/90)*8,32,128); atlas=18*tile; dispatch=tile/8',
                hooks=dict(initial=initial, comparison=comparison, query=query),
                writes=[dict(offset=at, bytes=len(raw), hex=raw.hex()) for at, raw in blocks+patches],
                original_recreation_path=True, executable_memory_written_at_runtime=False,
                pool_capacity_changed=False, graphics_quality_changed=True,
                classic_world_resolution_changed=False,
                runtime_rendering_verified=False, physical_memory_saving_verified=False)
