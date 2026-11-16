# INPUT: Prepared ASTC textures and their original descriptor entries.
# OUTPUT: Format-aware native layout helpers and byte-verified mip transforms.
# POS: Computer-side resource preparation; uses the renderer's actual NVN layout.
import base64
import ctypes as ct
import platform
import struct
import subprocess
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[2]
_bundle_sources = Path(__file__).resolve().parents[1] / 'preparer'
if not (ROOT / 'dm/gfx/nvn/nvn_tex.c').is_file() and (_bundle_sources / 'dm/gfx/nvn/nvn_tex.c').is_file():
    ROOT = _bundle_sources
WORLD_PREFIXES = tuple("data/hd/" + name + "/" for name in
                       ("env", "character", "items", "vfx", "objects", "missiles"))
FORMATS = {95: 0x79, 81: 0x87, 102: 0x80, 88: 0x8e}
BLOCK_EDGES = {95: 4, 81: 4, 102: 8, 88: 8}


def logical_path(value):
    path = PurePosixPath(value.replace("\\", "/").lower())
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise ValueError("unsafe logical asset path")
    return str(path)


def world_texture(value):
    path = logical_path(value)
    return path.startswith(WORLD_PREFIXES) and path.endswith(".texture")


def validate_fixed_atlas_sources(original, prepared):
    """Reject newly undersized square sources for the title's 128px atlas copy.

    Retained ASTC mip bytes can be valid while their new dimensions violate a
    later fixed-size GPU copy. This additional profile contract is separate
    from the generic mip trimmer and leaves originally small textures alone.
    """
    if len(original) != len(prepared):
        raise ValueError('atlas descriptor inventory changed')
    checked = 0
    for old, new in zip(original, prepared):
        if old['filename'] != new['filename']:
            raise ValueError('atlas descriptor identity changed')
        if (world_texture(old['filename']) and old['format'] in FORMATS and
                old['width'] == old['height'] and old['width'] >= 128):
            checked += 1
            if min(new['width'], new['height']) < 128:
                raise ValueError('fixed 128x128 atlas copy source reduced below 128: ' + old['filename'])
    return checked


class Descriptor(ct.Structure):
    _fields_ = [(name, ct.c_int) for name in ("target", "format", "flags", "samples")] + [
        (name, ct.c_uint32) for name in ("width", "height", "depth", "levels", "stride")
    ] + [("pkg_block_h", ct.c_int)]


class Layout(ct.Structure):
    _fields_ = [(name, ct.c_uint64) for name in ("size", "layer_size")] + [
        (name, ct.c_uint32) for name in ("align", "layers", "bpb", "tileH", "tileD")]


class NativeLayout:
    def __init__(self, work, compiler="cc"):
        if not (ROOT / 'dm/gfx/nvn/nvn_tex.c').is_file():
            raise ValueError('texture layout sources missing; use the full --with-preparer package or repository')
        work = Path(work)
        work.mkdir(parents=True, exist_ok=True)
        suffix = ".dylib" if platform.system() == "Darwin" else ".so"
        library = work / ("libnvn-layout" + suffix)
        shared = "-dynamiclib" if platform.system() == "Darwin" else "-shared"
        subprocess.run([compiler, shared, "-fPIC", "-O2", "-std=c11", "-Wall",
                        "-Wextra", "-Werror", str(ROOT / "dm/gfx/nvn/nvn_tex.c"),
                        str(ROOT / "dm/gfx/tex/nvn_deswizzle.c"), "-o", str(library)], check=True)
        self.lib = ct.CDLL(str(library.resolve()))
        self.lib.nvn_tex_layout.argtypes = [ct.POINTER(Descriptor), ct.POINTER(Layout)]
        self.lib.nvn_tex_level_offset.argtypes = [ct.POINTER(Descriptor), ct.c_uint]
        self.lib.nvn_tex_level_offset.restype = ct.c_uint64
        self.lib.nvn_tex_level_tiles.argtypes = [ct.POINTER(Descriptor), ct.c_uint,
                                               ct.POINTER(ct.c_uint), ct.POINTER(ct.c_uint)]
        self.lib.nvn_deswizzle.argtypes = [ct.POINTER(Descriptor), ct.c_uint, ct.c_uint,
                                         ct.c_void_p, ct.c_void_p, ct.c_size_t]
        self.lib.nvn_deswizzle.restype = ct.c_int
        self.lib.nvn_swizzle.argtypes = [ct.POINTER(Descriptor), ct.c_uint, ct.c_uint,
                                        ct.c_void_p, ct.c_size_t, ct.c_void_p]
        self.lib.nvn_swizzle.restype = ct.c_int
        self.lib.nvn_fmt_bpb.argtypes = [ct.c_int, ct.POINTER(ct.c_int), ct.POINTER(ct.c_int)]
        self.lib.nvn_fmt_bpb.restype = ct.c_int

    def layout(self, desc):
        result = Layout()
        self.lib.nvn_tex_layout(ct.byref(desc), ct.byref(result))
        return result

    def offsets(self, desc):
        return [self.lib.nvn_tex_level_offset(ct.byref(desc), i) for i in range(desc.levels)]

    def tiles(self, desc, level):
        height, depth = ct.c_uint(), ct.c_uint()
        self.lib.nvn_tex_level_tiles(ct.byref(desc), level, ct.byref(height), ct.byref(depth))
        return height.value, depth.value

    def linear(self, desc, level, storage):
        pitch, size = self.linear_geometry(desc, level)
        if len(storage) != self.layout(desc).size:
            raise ValueError("native texture storage size mismatch")
        result = ct.create_string_buffer(size)
        source = ct.create_string_buffer(storage)
        if self.lib.nvn_deswizzle(ct.byref(desc), level, 0, source, result, pitch):
            raise ValueError("native NVN deswizzle rejected texture")
        return result.raw

    def linear_geometry(self, desc, level):
        if not 0 <= level < desc.levels:
            raise ValueError("native texture mip level out of bounds")
        bw, bh = ct.c_int(), ct.c_int()
        bpb = self.lib.nvn_fmt_bpb(desc.format, ct.byref(bw), ct.byref(bh))
        if not bpb:
            raise ValueError("unsupported native texture format")
        width, height = max(1, desc.width >> level), max(1, desc.height >> level)
        pitch = ((width + bw.value - 1) // bw.value) * bpb
        return pitch, pitch * ((height + bh.value - 1) // bh.value)

    def swizzle(self, desc, mips):
        if len(mips) != desc.levels:
            raise ValueError("native texture mip count mismatch")
        storage = ct.create_string_buffer(self.layout(desc).size)
        for level, data in enumerate(mips):
            pitch, size = self.linear_geometry(desc, level)
            if len(data) != size:
                raise ValueError("native texture linear mip size mismatch")
            source = ct.create_string_buffer(data)
            if self.lib.nvn_swizzle(ct.byref(desc), level, 0, source, pitch, storage):
                raise ValueError("native NVN swizzle rejected texture")
        return storage.raw


def descriptor(entry):
    return Descriptor(1, FORMATS[entry["format"]], 0, 0, entry["width"], entry["height"],
                      1, entry["levels"], 0, (entry["layoutData"] & 255) + 1)


def read_texture(raw, entry, native):
    if len(raw) < 44 or raw[:4] != bytes.fromhex("3c444528"):
        raise ValueError("unsupported texture header")
    if entry["type"] != 2 or entry["depth"] != 1 or entry["layoutType"] != 3:
        raise ValueError("only packaged 2D textures are supported")
    width, height = struct.unpack_from("<II", raw, 8)
    levels = struct.unpack_from("<I", raw, 28)[0]
    if (raw[4], raw[6], raw[7], width, height, struct.unpack_from("<H", raw, 16)[0],
        raw[18], struct.unpack_from("<Q", raw, 19)[0], levels) != (
            entry["format"], 2, entry["levels"], entry["width"], entry["height"], 1,
            3, entry["layoutData"], entry["levels"]):
        raise ValueError("texture file and descriptor disagree")
    if not 1 <= levels <= 16 or not 0 < width <= 16384 or not 0 < height <= 16384:
        raise ValueError("texture dimensions/level count")
    if width & (width - 1) or height & (height - 1) or entry["layoutData"] & 255 > 5:
        raise ValueError("unsupported non-power-of-two texture or tile height")
    if struct.unpack_from("<i", raw, 32)[0] != 4 or 36 + 8 * levels > len(raw):
        raise ValueError("texture mip-table bounds")
    desc = descriptor(entry)
    layout = native.layout(desc)
    offsets = native.offsets(desc) + [layout.size]
    start = 36 + 8 * levels
    if layout.layers != 1 or start + layout.size != len(raw):
        raise ValueError("texture payload differs from native NVN layout")
    payloads = []
    for i in range(levels):
        pos = 36 + 8 * i
        size, relative = struct.unpack_from("<Ii", raw, pos)
        if pos + 4 + relative != start + offsets[i] or size != offsets[i + 1] - offsets[i]:
            raise ValueError("texture mip offset/size differs from native layout")
        payloads.append(raw[start + offsets[i]:start + offsets[i + 1]])
    return desc, raw[start:], payloads


def validate_thumbnail(entry):
    text = entry.get("thumbnailBase64")
    if not text:
        return
    data = base64.b64decode(text, validate=True)
    if len(data) < 8:
        raise ValueError("thumbnail header truncated")
    count, relative = struct.unpack_from("<Ii", data)
    table = 4 + relative
    first = entry["thumbnailMip"]
    if not 0 <= first < entry["levels"] or count != entry["levels"] - first:
        raise ValueError("thumbnail mip range")
    if table < 8 or table + count * 8 > len(data):
        raise ValueError("thumbnail table bounds")
    end = table + count * 8
    for i in range(count):
        pos = table + i * 8
        size, relative = struct.unpack_from("<Ii", data, pos)
        width, height = max(1, entry["width"] >> (first + i)), max(1, entry["height"] >> (first + i))
        block = BLOCK_EDGES[entry["format"]]
        expected = ((width + block - 1) // block) * ((height + block - 1) // block) * 16
        if pos + 4 + relative != end or size != expected or end + size > len(data):
            raise ValueError("thumbnail mip bounds/geometry")
        end += size
    if end != len(data):
        raise ValueError("thumbnail trailing data")


def trim_texture(raw, entry, maximum, native):
    """Retain existing ASTC mip blocks, proving every retained mip against C.

    Unsupported asset classes are kept by the caller. A supported file with
    malformed metadata fails the build instead of silently yielding a bad pack.
    """
    if maximum < 16:
        raise ValueError("maximum texture edge must be at least 16")
    skip = 0
    while max(max(1, entry["width"] >> skip), max(1, entry["height"] >> skip)) > maximum:
        skip += 1
    if not skip:
        return raw, dict(entry), 0
    if skip >= entry["levels"]:
        return raw, dict(entry), 0
    if entry.get("thumbnailBase64") and entry["thumbnailMip"] < skip:
        return raw, dict(entry), 0
    old_desc, old_storage, payloads = read_texture(raw, entry, native)
    validate_thumbnail(entry)
    tile_h, tile_d = native.tiles(old_desc, skip)
    if tile_d:
        raise ValueError("unexpected 2D texture depth tiling")
    changed = dict(entry, width=max(1, entry["width"] >> skip),
                   height=max(1, entry["height"] >> skip), levels=entry["levels"] - skip,
                   layoutData=(entry["layoutData"] & ~255) | tile_h)
    if "thumbnailMip" in changed:
        changed["thumbnailMip"] = max(0, entry["thumbnailMip"] - skip)
    header = bytearray(raw[:36])
    header[7] = changed["levels"]
    struct.pack_into("<II", header, 8, changed["width"], changed["height"])
    struct.pack_into("<Q", header, 19, changed["layoutData"])
    struct.pack_into("<I", header, 28, changed["levels"])
    retained = payloads[skip:]
    position = 36 + 8 * len(retained)
    table = bytearray()
    for i, mip in enumerate(retained):
        table.extend(struct.pack("<Ii", len(mip), position - (40 + 8 * i)))
        position += len(mip)
    result = bytes(header + table) + b"".join(retained)
    new_desc, new_storage, _ = read_texture(result, changed, native)
    validate_thumbnail(changed)
    for i in range(changed["levels"]):
        if native.linear(old_desc, i + skip, old_storage) != native.linear(new_desc, i, new_storage):
            raise ValueError("retained ASTC mip changed after repacking")
    return result, changed, skip


def pixel_texture(raw, entry, native):
    """Build a valid 1x1 placeholder from the smallest authored ASTC block.

    This is an explicit destructive-quality experiment, separate from mip
    trimming. Both the package and its thumbnail describe the new geometry;
    an old large thumbnail must not silently restore a larger allocation.
    """
    old_desc, old_storage, _ = read_texture(raw, entry, native)
    validate_thumbnail(entry)
    block = native.linear(old_desc, old_desc.levels - 1, old_storage)[:16]
    if len(block) != 16:
        raise ValueError("one complete ASTC block required")
    changed = dict(entry, width=1, height=1, levels=1,
                   layoutData=entry["layoutData"] & ~255)
    desc = descriptor(changed)
    layout = native.layout(desc)
    if layout.layers != 1 or layout.size < 16 or native.offsets(desc) != [0]:
        raise ValueError("unexpected 1x1 native texture layout")
    # A single compressed block occupies the first bytes of the first GOB.
    # Verify this through the native deswizzler, rather than assuming a pitch.
    storage = block + bytes(layout.size - len(block))
    header = bytearray(raw[:36])
    header[7] = 1
    struct.pack_into("<II", header, 8, 1, 1)
    struct.pack_into("<Q", header, 19, changed["layoutData"])
    struct.pack_into("<I", header, 28, 1)
    result = bytes(header) + struct.pack("<Ii", layout.size, 4) + storage
    if "thumbnailMip" in changed or "thumbnailBase64" in changed:
        changed["thumbnailMip"] = 0
        changed["thumbnailBase64"] = base64.b64encode(
            struct.pack("<IiIi", 1, 4, 16, 4) + block).decode("ascii")
    new_desc, new_storage, _ = read_texture(result, changed, native)
    validate_thumbnail(changed)
    if native.linear(new_desc, 0, new_storage) != block:
        raise ValueError("1x1 ASTC block changed after repacking")
    return result, changed
