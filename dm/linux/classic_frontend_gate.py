# INPUT: Guarded 90447/94012 frontend roots and reserved native-v6 RX/RW padding.
# OUTPUT: A three-panel resource lease that preserves original open/close bodies.
# POS: Optional menu restoration; original cleanup and GPU release fences remain.
"""Allow frontend resources while an original menu root is open.

The shared Widget callbacks also run during initialization and cannot identify
a frontend lifetime. Hook the three reviewed root overrides instead. A bit per
panel tolerates repeated opens and overlapping panel handoffs without counting
the same panel twice. Closing the last root restores the classic resource gate
before its original cleanup runs. No resource is forcibly freed here.
"""
import hashlib
import struct

OFFSET = 0x600
STRIDE = 112
BRANCH_OFFSET = 104
CALLBACKS = (
    ("MainMenuPanel", "open", 0xa17d9c, "ffc301d1f71b00f9f65704a9f44f05a9fd7b06a9fd8301912810019036008052"),
    ("MainMenuPanel", "close", 0xa18b20, "f50f1df8f44f01a9fd7b02a9fd830091084041f91fe009391f01003914b841f9"),
    ("CharacterSelectPanel", "open", 0xa25f30, "ff4301d1f65702a9f44f03a9fd7b04a9fd030191350d0190a82640b9f30300aa"),
    ("CharacterSelectPanel", "close", 0xa2634c, "ff4301d1f65702a9f44f03a9fd7b04a9fd03019108b440f9c90c019029e11891"),
    ("CharacterCreatePanel", "open", 0xac52a4, "ff0302d1fa6703a9f85f04a9f65705a9f44f06a9fd7b07a9fdc30191a80a01d0"),
    ("CharacterCreatePanel", "close", 0xac6a28, "ff8301d1f85f02a9f65703a9f44f04a9fd7b05a9fd430191d51301d0a87647f9"),
)
CALLBACKS_94012 = (
    ("MainMenuPanel", "open", 0x805bac, "ff8302d1fa6705a9f85f06a9f65707a9f44f08a9fd7b09a9fd430291482e01f0"),
    ("MainMenuPanel", "close", 0x806b94, "ff8301d1f65703a9f44f04a9fd7b05a9fd430191c80280d20800f0f2565001f0"),
    ("CharacterSelectPanel", "open", 0x829dc4, "ff4301d1f65702a9f44f03a9fd7b04a9fd030191952a01d0a8de4ab9f30300aa"),
    ("CharacterSelectPanel", "close", 0x82a1c8, "ff4301d1f65702a9f44f03a9fd7b04a9fd03019108b840f9292a01f029c10391"),
    ("CharacterCreatePanel", "open", 0xc22b04, "ffc302d1fa6706a9f85f07a9f65708a9f44f09a9fd7b0aa9fd830291680d01d0"),
    ("CharacterCreatePanel", "close", 0xc24a20, "ff8301d1f85f02a9f65703a9f44f04a9fd7b05a9fd430191942701f094e22491"),
)
# Assembled from classic_frontend_gate.S.in; branch destinations are relocated
# below for both supported native-v6 image layouts. No user assembler is needed.
TEMPLATE = bytes.fromhex(
    "e92bbda9eb3301a90c423bd5ec1300f9895700102a0101914b7d5f886b0100324b7d0c88acffff357f0100712b008052"
    "6c0080526b118c1a2b0100b92ac100914b7d5f886b0500114b7d0c88acffff35201d00f9ec1340f90c421bd5eb3341a9"
    "e92bc3a8ffc301d1ce115e1700000000e92bbda9eb3301a90c423bd5ec1300f9095400102a0101914b7d5f886b791f12"
    "4b7d0c88acffff357f0100712b0080526c0080526b118c1a2b0100b92ad100914b7d5f886b0500114b7d0c88acffff35"
    "201d00f9ec1340f90c421bd5eb3341a9e92bc3a8f50f1df813155e1700000000e92bbda9eb3301a90c423bd5ec1300f9"
    "895000102a0101914b7d5f886b011f324b7d0c88acffff357f0100712b0080526c0080526b118c1a2b0100b92ac10091"
    "4b7d5f886b0500114b7d0c88acffff35201d00f9ec1340f90c421bd5eb3341a9e92bc3a8ff4301d1fb495e1700000000"
    "e92bbda9eb3301a90c423bd5ec1300f9094d00102a0101914b7d5f886b791e124b7d0c88acffff357f0100712b008052"
    "6c0080526b118c1a2b0100b92ad100914b7d5f886b0500114b7d0c88acffff35201d00f9ec1340f90c421bd5eb3341a9"
    "e92bc3a8ff4301d1e64a5e1700000000e92bbda9eb3301a90c423bd5ec1300f9894900102a0101914b7d5f886b011e32"
    "4b7d0c88acffff357f0100712b0080526c0080526b118c1a2b0100b92ac100914b7d5f886b0500114b7d0c88acffff35"
    "201d00f9ec1340f90c421bd5eb3341a9e92bc3a8ff0302d1a0c6601700000000e92bbda9eb3301a90c423bd5ec1300f9"
    "094600102a0101914b7d5f886b791d124b7d0c88acffff357f0100712b0080526c0080526b118c1a2b0100b92ad10091"
    "4b7d5f886b0500114b7d0c88acffff35201d00f9ec1340f90c421bd5eb3341a9e92bc3a8ff8301d165cc6017"
)


def frontend_gate_patches(image, stub, control, branch, game_build='90447'):
    if game_build not in ('90447', '94012'):
        raise ValueError('unsupported frontend callback build')
    profile = CALLBACKS_94012 if game_build == '94012' else CALLBACKS
    if control - stub != 0x1100 or OFFSET + len(TEMPLATE) > 0xf00:
        raise ValueError('frontend gate requires reserved native-v6 layout')
    for _, _, site, context in profile:
        expected = bytes.fromhex(context)
        if image[site:site+len(expected)] != expected:
            raise ValueError('unreviewed frontend callback at 0x%x' % site)
    code = bytearray(TEMPLATE)
    patches, callbacks = [], []
    for index, (name, event, site, context) in enumerate(profile):
        target = stub + OFFSET + index * STRIDE
        at = index * STRIDE + BRANCH_OFFSET
        code[at-4:at] = bytes.fromhex(context)[:4]
        struct.pack_into('<I', code, at, branch(stub+OFFSET+at, site+4))
        patches.append((site, struct.pack('<I', branch(site, target))))
        callbacks.append(dict(panel=name, event=event, site=site, stub=target,
                              lease_bit=1 << (index // 2)))
    patches.append((stub+OFFSET, bytes(code)))
    return patches, dict(policy='frontend-root-resource-lease-'+game_build+'-v1',
        callbacks=callbacks, code_bytes=len(code),
        code_sha256=hashlib.sha256(code).hexdigest(),
        initial_mode=1, frontend_mode=1, gameplay_mode=3,
        lease_bits_offset=64, opened_count_offset=48, closed_count_offset=52,
        last_panel_offset=56, original_callbacks_preserved=True,
        original_model_release_preserved=True, forced_resource_free=False,
        runtime_rendering_verified=False)

