# INPUT: Reviewed 90447 native-v6 image with unused RX padding and RW control space.
# OUTPUT: File, inline-thumbnail/descriptor and 3D-preview gates plus a receipt.
# POS: Optional classic-only offline policy; original resources and runtime code stay immutable.
"""Cut HD world resources before archive open, read allocation and decoding.

This policy is specific to the reviewed executable build, not individual asset
names. New world assets under data/hd/ are denied automatically. Shared UI,
sound, video, shaders and descriptor/index resources retain the original read
path. Only reviewed shared global subcategories pass; HD preload/LOD/FX tables
at the global root and unknown new global categories are denied as well.
By default the three 3D widget types are omitted by the original nullable factory.
Embedded player loaders keep child UI updates while their HD resource state
machine is skipped before model creation, including the class-selection page.
Mixed texture descriptor entries use the same path classifier before persistent
allocation and embedded thumbnail decoding; their shared file and JSON parse remain.
The optional frontend policy preserves widget construction and original root
open/close callbacks, allowing resources while a frontend root is open and
restoring the classic gate when the last root closes. The 94012 creation
camera and returning main menu read their visual configurations between those
root callbacks; those exact frontend metadata files remain readable across the handoff.
"""
import hashlib
import struct
from classic_frontend_gate import frontend_gate_patches

POLICY = 'classic-resource-gate-90447-v3'
FILE_SITE = 0xC7D7C0
WIDGET_SITES = (0xA3F5F8, 0xA3F608)
LOOKUP = 0x98AAE4
PREVIEW_SITE = 0x9AD8E0
PREVIEW_CHILDREN = 0x9ADEA8
DESCRIPTOR_SITE = 0x1546170
DESCRIPTOR_SKIP = 0x1546010
DESCRIPTOR_ZERO = 0x1546174
DESCRIPTOR_NONZERO = 0x1546198
TEMPLATE_BASE = 0x3293000
WIDGET_OFFSET = 0x360
PREVIEW_OFFSET = 0x460
DESCRIPTOR_OFFSET = 0x4d0
CLASSIFIER_OFFSET = 0x124
FRONTEND_TABLE_OFFSET = 0x900
CLASSIFIER_TABLE_ADR_OFFSET = 0x158
FRONTEND_TRANSITION_METADATA_94012 = (
    'env/vis/ui_frontend_charactercreate.json',
    'env/vis/ui_frontend_act1_vignette.json',
    'env/vis/ui_frontend_act2_vignette.json',
    'env/vis/ui_frontend_act3_vignette.json',
    'env/vis/ui_frontend_act4_vignette.json',
    'env/vis/ui_frontend_act5_vignette.json',
)
BRANCH_FIXUPS = ((0x120, FILE_SITE + 4),
                 (0x41C, LOOKUP),
                 (0x4B0, PREVIEW_CHILDREN),
                 (0x4C0, PREVIEW_SITE + 4),
                 (0x54C, DESCRIPTOR_SKIP),
                 (0x56C, DESCRIPTOR_ZERO),
                 (0x570, DESCRIPTOR_NONZERO))
PRESERVED_PREFIXES = ('global/ui/', 'global/sfx/', 'global/music/', 'global/video/',
                      'global/excel/', 'global/palette/', 'global/textures/',
                      'shaders/', 'local/', 'ui/', 'env/weather/',
                      'env/lut/', 'vfx/textures/default/')
PRESERVED_PREFIXES += ('vfx/particles/ui/', 'vfx/meshes/lit_mesh/free_target_reticle/',
                       'vfx/textures/debug/', 'vfx/textures/gradient/')
PRESERVED_METADATA = ('items/items.json', 'items/sets.json', 'items/uniques.json',
                      'texture_desc_cache.json', 'model_lod_desc.bin',
                      'model_lod_desc.json', 'pak_manifest.json')
WIDGET_TYPES = ('Camera3DWidget', 'PlayerLoaderWidget', 'PlayerMoverWidget')
COUNTERS = ('total', 'non_hd', 'hd_infrastructure', 'hd_world', 'denied',
            'widget_seen', 'widget_denied', 'preview_seen', 'preview_skipped',
            'descriptor_seen', 'descriptor_skipped')

# Guard the open prologue, its allocation-free failure path, and both nullable
# JSON-factory lookup paths and child insertion. All checks precede any write.
CONTEXT = (
    (FILE_SITE, 'fc4fbea9fd7b01a9fd430091ffc30cd1e20300aa21608052e0730091f3730091'),
    (0xC99BF0, 'f40300aae00302aae1031f2af50303aaf08eff97a00200f9c00700b4080040f9081d40f900013fd6f50300aa9a420591'),
    (0xC99D00, 'fd7b45a9f44f44a9f65743a9f85f42a9fa6741a9fc6fc6a8c0035fd6'),
    (0xA3F5F0, 'a8006037401140f93b2dfd97a00000b5f2ffff1740610091372dfd97e0fdffb4a80640f9f60300aaa90240b9090400342a05098bcb2d8c52abadac72ac028052'),
    (0xA3FEAC, '95fdff97800000b4e10300aae00314aa8eccff97e82f40f9'),
    (0x9AD8C0, 'ff0302d1fc6f02a9fa6703a9f85f04a9f65705a9f44f06a9fd7b07a9fdc30191088c4eb9f30300aa1f0d0071e82d0054a99300b0291130918a0000102b7968784a090b8b40011fd660464dbd2800805261e23491614a4dbd62220291688e0eb9608640b9271ef697604a0eb94130f697'),
    (0x9ADEA8, '6842413908020034683240f9c80100b4732e40f914f17dd30400001473220091942200f100010054600240f90840413968ffff34080040f9080d40f900013fd6f7ffff17fd7b47a9f44f46a9f65745a9f85f44a9fa6743a9fc6f42a9ff030291c0035fd6'),
    # Class-selection transitions already treat an absent preview as complete.
    (0x73A14C, '1f00017128060054'),
    (0x73A214, '20008052fd7b41a9f44fc2a8c0035fd6'),
    # Mixed descriptor tables retain their shared file/JSON parse. The entry
    # gate runs before persistent descriptor allocation or inline thumbnail decode.
    (0x1546154, '7f0109ebc00100546c6100916d1140f96bbd40397f011c72b5018c9a4a010035eb0308aa7f0109eb010500542e000014eb0308aa7f0109eb81feff54f5031faa0affff344b050a8b6ced7cd3eb0308aa'),
    (0x1546008, '568b00f916d900d0e8cf40f90a0380529c630091e99341b928210a9b900e8dd2b0aeadf2c2a47414d0a47414834e8ed2'),
    (0x1546850, '086941f93f7d00a9880000b5e8d800f0006541f9ea2e0094e8d800f008c1079118210091e00318aaf70308aa61721294e00240f901018052e2031baa55251294f70300aae00318aa62721294f8220091e2e30191ff0200f9e00318aae10315aaa5dbba97'),
    (0x1546F48, 'e83340b95fa303390886ff34e01740f97e6d1294a085ffb4e82740b9480704390804008b18fd42d3f85b01f9488b40f9f70300aa552304911f0118eb6801005462010054e00315aae10318aa3e2e009448a750a90001098b0801188b020100cbe1031f2a616d1294588b00f9408740f9e1c30a91e30317aae21740f9db431294'),
    (0x1545D64, '080040f9080940f90130a052020280521530a05200013fd6f40300aa'),
)

# Generated only from our companion assembly source, not copied guest code.
# Retain an assembler-free desktop preparer; tests compare this with assembled
# source and execute these instructions under Unicorn, including ASLR and ABI.
TEMPLATE = bytes.fromhex(
    "e92bbca9eb3301a9f04702a90c423bd5ec1b00f9fe1f00f950870010110240b931070036000700b4111200912a7e5f88"
    "4b0500112b7e0c88acffff353a000094910000343f06007180000054090000141122009102000014113200912a7e5f88"
    "4b0500112b7e0c88acffff3526000014114200912a7e5f884b0500112b7e0c88acffff355f810071020200540b188052"
    "4a7dab9b0a020a8b4a010491ec1f40f94c0100f94a410091e90300aa111480522b1540384b1500388b00003431060071"
    "81ffff545f010039110240b9d1010836115200912a7e5f884b0500112b7e0c88acffff35fe1f40f9ec1b40f90c421bd5"
    "f04742a9eb3341a9e92bc4a8e0031faac0035fd6fe1f40f9ec1b40f90c421bd5f04742a9eb3341a9e92bc4a8fc4fbea9"
    "a9a96717200400b4e90300aaaa0400104b1540382b0100342c1540389f710171f10580522c028c1a8c011b327f010c6b"
    "c1020054f7ffff178a0300304b154038cb020034092000914c154038311540383f72017141000054f10580526c000034"
    "31021b328c011b329f01116b810000546b050071a1feff54060000146b0500514a010b8beeffff1711008052c0035fd6"
    "31008052c0035fd651008052c0035fd6646174612f68642f000a676c6f62616c2f75692f0b676c6f62616c2f7366782f"
    "0d676c6f62616c2f6d757369632f0d676c6f62616c2f766964656f2f0d676c6f62616c2f657863656c2f0f676c6f6261"
    "6c2f70616c657474652f10676c6f62616c2f74657874757265732f08736861646572732f066c6f63616c2f0375692f0c"
    "656e762f776561746865722f08656e762f6c75742f117666782f7061727469636c65732f75692f287666782f6d657368"
    "65732f6c69745f6d6573682f667265655f7461726765745f72657469636c652f137666782f74657874757265732f6465"
    "6275672f167666782f74657874757265732f6772616469656e742f157666782f74657874757265732f64656661756c74"
    "2f116974656d732f6974656d732e6a736f6e00106974656d732f736574732e6a736f6e00136974656d732f756e697175"
    "65732e6a736f6e0018746578747572655f646573635f63616368652e6a736f6e00136d6f64656c5f6c6f645f64657363"
    "2e62696e00146d6f64656c5f6c6f645f646573632e6a736f6e001270616b5f6d616e69666573742e6a736f6e00000000"
    "e92bbca9eb3301a9f04702a90c423bd5ec1b00f9706c0010110240b971040036400400b4ea0400104b154038eb030034"
    "e90300aa4c154038311540389f01116b810000546b05007161ffff54040000146b0500514a010b8bf4ffff1711620091"
    "2a7e5f884b0500112b7e0c88acffff35110240b9b1010836117200912a7e5f884b0500112b7e0c88acffff35ec1b40f9"
    "0c421bd5f04742a9eb3341a9e92bc4a8e0031faac0035fd6ec1b40f90c421bd5f04742a9eb3341a9e92bc4a8b2dd5b17"
    "0f43616d65726133445769646765740013506c617965724c6f616465725769646765740012506c617965724d6f766572"
    "57696467657400001f2003d51f2003d5e92bbea9f04701a9d0640010110240b93102003611820091297e5f882a050011"
    "2a7e0988a9ffff35110240b95101083611920091297e5f882a0500112a7e0988a9ffff35f04741a9e92bc2a8f30300aa"
    "7e6a5c17f04741a9e92bc2a8088c4eb909695c171f2003d51f2003d51f2003d5e07bbba9e92b01a9eb3302a9f04703a9"
    "0c423bd5ec2300f9d0600010110240b91103003611a200912a7e5f884b0500112b7e0c88acffff35e00315aa06ffff97"
    "3f0a0071e1010054110240b9b101083611b200912a7e5f884b0500112b7e0c88acffff35ec2340f90c421bd5f04743a9"
    "eb3342a9e92b41a9e07bc5a8b1ca8a17ec2340f90c421bd5f04743a9eb3342a9e92b41a9e07bc5a84a00003502cb8a17"
    "0acb8a17"
)


def branch(pc, target, link=False):
    delta = target - pc
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError('resource gate branch outside ARM64 immediate range')
    return (0x94000000 if link else 0x14000000) | ((delta // 4) & 0x3ffffff)


def prepare_resource_gate(image, mode=3, frontend=False, game_build="90447"):
    """Modify a native-v6 bytearray after all build/layout/collision guards pass.

    Mode 0 preserves all original reads/constructors, 1 audits, 3 denies. The
    public classic-only option uses 3; modes 0/1 support controlled comparisons.
    This never changes bytes in the source asset archives or executable live.
    """
    if game_build not in ('90447', '94012'):
        raise ValueError('unsupported classic resource build')
    if game_build == '94012':
        import classic_resource_94012
        profile = vars(classic_resource_94012)
        magic, sites, patch_off = b'D2RHD940', 3, 0x3220000
    else:
        profile = globals()
        magic, sites, patch_off = b'D2RHD904', 8, 0x3070000
    POLICY, FILE_SITE, WIDGET_SITES, LOOKUP, PREVIEW_SITE, PREVIEW_CHILDREN, DESCRIPTOR_SITE, DESCRIPTOR_SKIP, DESCRIPTOR_ZERO, DESCRIPTOR_NONZERO, BRANCH_FIXUPS, CONTEXT = (profile[k] for k in
        ('POLICY', 'FILE_SITE', 'WIDGET_SITES', 'LOOKUP', 'PREVIEW_SITE', 'PREVIEW_CHILDREN', 'DESCRIPTOR_SITE', 'DESCRIPTOR_SKIP', 'DESCRIPTOR_ZERO', 'DESCRIPTOR_NONZERO', 'BRANCH_FIXUPS', 'CONTEXT'))
    if type(mode) is not int or mode not in (0, 1, 3):
        raise ValueError('resource gate mode must be 0, 1 or 3')
    if type(frontend) is not bool or frontend and mode != 3:
        raise ValueError('frontend resources require the normal classic gate mode')
    if not isinstance(image, bytearray) or len(image) % 0x4000 or not patch_off+0x8000 <= len(image) <= 64*1024**2:
        raise ValueError('resource gate requires prepared native-v6 modules')
    control = len(image) - 0x4000
    header, stub = control - 256, control - 0x1000
    if (image[header:header+8] != magic or
            struct.unpack_from('<II', image, header+8) != (6, sites) or
            struct.unpack_from('<I', image, header+24)[0] != control):
        raise ValueError('resource gate requires native-v6 trailer')
    source = struct.unpack_from('<Q', image, header+16)[0]
    native_end = ((source+15) & ~15) + sites*48
    if not patch_off <= source < native_end <= stub or len(TEMPLATE) > header-stub:
        raise ValueError('resource gate requires reserved executable padding')
    if any(image[stub:header]) or any(image[control+0x100:]):
        raise ValueError('resource gate padding/control overlaps another policy')
    for at, raw in CONTEXT:
        expected = bytes.fromhex(raw)
        if image[at:at+len(expected)] != expected:
            raise ValueError('unreviewed/already changed resource gate context at 0x%x' % at)
    code = bytearray(TEMPLATE)
    for at, target in BRANCH_FIXUPS:
        struct.pack_into('<I', code, at, branch(stub+at, target))
    patches = [(FILE_SITE, struct.pack('<I', branch(FILE_SITE, stub)))]
    if not frontend:
        patches += [(at, struct.pack('<I', branch(at, stub+WIDGET_OFFSET, True))) for at in WIDGET_SITES]
    patches += [(PREVIEW_SITE, struct.pack('<I', branch(PREVIEW_SITE, stub+PREVIEW_OFFSET)))]
    patches += [(DESCRIPTOR_SITE, struct.pack('<I', branch(DESCRIPTOR_SITE, stub+DESCRIPTOR_OFFSET)))]
    frontend_policy = None
    transition_metadata = ()
    frontend_table = b''
    if frontend:
        added, frontend_policy = frontend_gate_patches(image, stub, control+0x100, branch, game_build)
        patches += added
        if game_build == '94012':
            transition_metadata = FRONTEND_TRANSITION_METADATA_94012
            entries = [s.encode() for s in PRESERVED_PREFIXES]
            entries += [s.encode()+b'\0' for s in PRESERVED_METADATA+transition_metadata]
            frontend_table = b''.join(bytes([len(s)])+s for s in entries)+b'\0'
            if FRONTEND_TABLE_OFFSET+len(frontend_table) > 0xf00:
                raise ValueError('frontend resource classifier table overlaps native trailer')
            if any(at < stub+FRONTEND_TABLE_OFFSET+len(frontend_table) and
                   stub+FRONTEND_TABLE_OFFSET < at+len(raw) for at, raw in added):
                raise ValueError('frontend resource classifier table overlaps callbacks')
            # Relocate only the allow table. The original classifier still
            # enforces normalized, exact metadata matches and rejects suffixes.
            at = CLASSIFIER_TABLE_ADR_OFFSET
            delta = FRONTEND_TABLE_OFFSET-at
            adr = 0x1000000a | ((delta & 3) << 29) | (((delta >> 2) & 0x7ffff) << 5)
            struct.pack_into('<I', code, at, adr)
            patches.append((stub+FRONTEND_TABLE_OFFSET, frontend_table))
    initial_mode = 1 if frontend else mode
    patches += [(stub, bytes(code)), (control+0x100, struct.pack('<I', initial_mode))]
    for at, raw in patches:
        image[at:at+len(raw)] = raw
    return dict(policy='classic-frontend-resource-gate-'+game_build+'-v1' if frontend else POLICY,
                mode=initial_mode, site=FILE_SITE, stub=stub, frontend_policy=frontend_policy,
                control=control+0x100, code_bytes=len(code), code_sha256=hashlib.sha256(code).hexdigest(),
                widget_stub=stub+WIDGET_OFFSET, widget_sites=list(WIDGET_SITES), widget_types=list(WIDGET_TYPES),
                preview_site=PREVIEW_SITE, preview_stub=stub+PREVIEW_OFFSET,
                preview_resume=PREVIEW_SITE+4, preview_children=PREVIEW_CHILDREN,
                embedded_preview_before_model_creation=True, embedded_preview_child_ui_preserved=True,
                descriptor_site=DESCRIPTOR_SITE, descriptor_stub=stub+DESCRIPTOR_OFFSET,
                descriptor_skip=DESCRIPTOR_SKIP, descriptor_zero=DESCRIPTOR_ZERO, descriptor_nonzero=DESCRIPTOR_NONZERO,
                descriptor_before_persistent_allocation=True, descriptor_before_thumbnail_decode=True,
                shared_descriptor_json_read_and_parse_retained=True, texture_metadata_arena_bytes_unchanged=0x1800000,
                classifier=stub+CLASSIFIER_OFFSET, counters=list(COUNTERS), counters_offset=4,
                record_count=32, record_stride=192, records_offset=256,
                preserved_prefixes=['data/hd/'+s for s in PRESERVED_PREFIXES],
                preserved_metadata=['data/hd/'+s for s in PRESERVED_METADATA+transition_metadata],
                frontend_transition_metadata=['data/hd/'+s for s in transition_metadata],
                frontend_classifier_table=stub+FRONTEND_TABLE_OFFSET if frontend_table else None,
                frontend_classifier_table_sha256=hashlib.sha256(frontend_table).hexdigest() if frontend_table else None,
                blocked_prefix='data/hd/ except shared infrastructure and metadata',
                before_archive_open=True, before_read_allocation=True, before_decode=True,
                widget_before_constructor=not frontend, flat_menu_required=not frontend,
                archive_files_changed=False, all_hd_resources_disabled=False,
                runtime_rendering_verified=False, physical_memory_saving_verified=False)
