# INPUT: Synthetic texture/shader fixtures and the renderer's native layout code.
# OUTPUT: Retained-mip equivalence, corruption rejection and safe-install checks.
# POS: Host-only preprocessing tests; no proprietary assets or device required.
import base64
import ctypes as ct
import gzip
import hashlib
import json
import random
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from handheld_mips import Descriptor, NativeLayout, read_texture, trim_texture, world_texture, pixel_texture, validate_thumbnail, validate_fixed_atlas_sources
from prepare_handheld import prepare_shaders, shader_index_keys
from install_handheld_pack import install


def digest(data):
    return hashlib.sha256(data).hexdigest()


class FixedAtlasTests(unittest.TestCase):
    def test_valid_mips_can_still_violate_the_runtime_atlas_copy(self):
        old = dict(filename='data/hd/env/texture/tile.texture', format=95, width=128, height=128)
        with self.assertRaisesRegex(ValueError, 'fixed 128x128'):
            validate_fixed_atlas_sources([old], [dict(old, width=64, height=64)])
        self.assertEqual(validate_fixed_atlas_sources([old], [old]), 1)

    def test_large_sources_keep_atlas_floor_and_authored_small_sources_survive(self):
        large = dict(filename='data/hd/character/player/armor.texture', format=95, width=1024, height=1024)
        small = dict(filename='data/hd/env/texture/small.texture', format=95, width=32, height=32)
        self.assertEqual(validate_fixed_atlas_sources([large, small], [dict(large, width=128, height=128), small]), 1)

    def test_missing_or_reordered_descriptor_identity_is_rejected(self):
        old = dict(filename='data/hd/env/texture/tile.texture', format=95, width=128, height=128)
        with self.assertRaises(ValueError):
            validate_fixed_atlas_sources([old], [])
        with self.assertRaises(ValueError):
            validate_fixed_atlas_sources([old], [dict(old, filename='data/hd/other.texture')])


class MipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.native = NativeLayout(cls.temporary.name)
        cls.native.lib.nvn_swizzle.argtypes = [ct.POINTER(Descriptor), ct.c_uint, ct.c_uint,
                                              ct.c_void_p, ct.c_size_t, ct.c_void_p]

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def fixture(self, width=256, height=256):
        desc = Descriptor(1, 121, 0, 0, width, height, 1, 9, 0, 4)
        layout = self.native.layout(desc)
        storage = ct.create_string_buffer(layout.size)
        random_bytes = random.Random(1928)
        linear = []
        for level in range(9):
            w, h = max(1, width >> level), max(1, height >> level)
            pitch = ((w + 3) // 4) * 16
            pixels = random_bytes.randbytes(pitch * ((h + 3) // 4))
            linear.append(pixels)
            data = ct.create_string_buffer(pixels)
            self.assertEqual(self.native.lib.nvn_swizzle(ct.byref(desc), level, 0, data, pitch, storage), 0)
        entry = dict(filename="data/hd/env/test.texture", type=2, width=width, height=height,
                     depth=1, levels=9, format=95, layoutType=3, layoutData=0x1000700000003,
                     thumbnailMip=4)
        thumbnail = bytearray(struct.pack("<Ii", 5, 4))
        start = 8 + 5 * 8
        for i, data in enumerate(linear[4:]):
            thumbnail.extend(struct.pack("<Ii", len(data), start - (12 + i * 8)))
            start += len(data)
        thumbnail.extend(b"".join(linear[4:]))
        entry["thumbnailBase64"] = base64.b64encode(thumbnail).decode()
        header = bytearray(36)
        header[:4] = bytes.fromhex("3c444528")
        header[4], header[6], header[7], header[18] = 95, 2, 9, 3
        struct.pack_into("<IIH", header, 8, width, height, 1)
        struct.pack_into("<Q", header, 19, entry["layoutData"])
        struct.pack_into("<II", header, 28, 9, 4)
        offsets = self.native.offsets(desc) + [layout.size]
        for i in range(9):
            header.extend(struct.pack("<Ii", offsets[i + 1] - offsets[i], 108 + offsets[i] - (40 + i * 8)))
        return bytes(header) + storage.raw, entry, linear

    def test_retained_mips_and_thumbnail_are_exact(self):
        for dimensions in [(256, 256), (256, 128)]:
            raw, entry, expected = self.fixture(*dimensions)
            result, changed, skip = trim_texture(raw, entry, 64, self.native)
            self.assertEqual(skip, 2)
            self.assertEqual(changed["thumbnailMip"], 2)
            self.assertEqual(changed["thumbnailBase64"], entry["thumbnailBase64"])
            self.assertEqual(changed["width"], 64)
            desc, storage, _ = read_texture(result, changed, self.native)
            for i in range(desc.levels):
                self.assertEqual(self.native.linear(desc, i, storage), expected[i + 2])

    def test_descriptor_mismatch_rejected(self):
        raw, entry, _ = self.fixture()
        entry["height"] = 128
        with self.assertRaisesRegex(ValueError, "disagree"):
            trim_texture(raw, entry, 128, self.native)

    def test_table_corruption_rejected(self):
        raw, entry, _ = self.fixture()
        broken = bytearray(raw)
        struct.pack_into("<i", broken, 40, 0x7fffffff)
        with self.assertRaisesRegex(ValueError, "offset/size"):
            trim_texture(broken, entry, 128, self.native)
        with self.assertRaises(ValueError):
            trim_texture(raw[:-1], entry, 128, self.native)

    def test_thumbnail_corruption_rejected(self):
        raw, entry, _ = self.fixture()
        entry["thumbnailBase64"] = base64.b64encode(b"invalid!").decode()
        with self.assertRaises(ValueError):
            trim_texture(raw, entry, 128, self.native)

    def test_ui_fonts_and_sprites_are_excluded(self):
        for path in ["data/global/ui/test.texture", "data/hd/ui/font.texture",
                     "data/hd/env/test.sprite", "data/hd/fonts/atlas.texture"]:
            self.assertFalse(world_texture(path))
        self.assertTrue(world_texture("data/hd/character/test.texture"))
        with self.assertRaises(ValueError):
            world_texture("data/hd/env/../../../outside.texture")

    def test_pixel_placeholder_keeps_smallest_block_and_replaces_thumbnail(self):
        for width, height in ((256, 256), (256, 128)):
            raw, entry, linear = self.fixture(width, height)
            old = dict(entry)
            result, changed = pixel_texture(raw, entry, self.native)
            self.assertEqual(entry, old)
            self.assertEqual((changed['width'], changed['height'], changed['levels']), (1, 1, 1))
            desc, storage, _ = read_texture(result, changed, self.native)
            self.assertEqual(self.native.linear(desc, 0, storage), linear[-1][:16])
            self.assertEqual(changed['thumbnailMip'], 0)
            validate_thumbnail(changed)
            self.assertEqual(base64.b64decode(changed['thumbnailBase64'])[16:], linear[-1][:16])
            self.assertLess(len(result), len(raw))

    def test_pixel_placeholder_rejects_corrupt_source_and_thumbnail(self):
        raw, entry, _ = self.fixture()
        with self.assertRaises(ValueError):
            pixel_texture(raw[:-1], entry, self.native)
        entry['thumbnailBase64'] = base64.b64encode(b'bad').decode()
        with self.assertRaises(ValueError):
            pixel_texture(raw, entry, self.native)


class ShaderTests(unittest.TestCase):
    def fixture(self, path):
        key = "0123456789abcdef"
        control = bytearray(0x880)
        struct.pack_into("<3I", control, 0, 0x98761234, 1, 16)
        struct.pack_into("<I", control, 0x704, 64)
        index = struct.pack("<3I", 0x494b5644, 2, 1) + key.encode() + struct.pack("<II", 0, 64) + control + bytes(64)
        raw = struct.pack("<5I", 0x07230203, 0x10000, 0, 1, 0)
        metadata = struct.pack("<12I", 0x4d4b5644, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, len(raw))
        compressed = gzip.compress(raw, mtime=0)
        (path / "shader_index.bin").write_bytes(index)
        (path / (key + ".spv.gz")).write_bytes(compressed)
        (path / (key + ".vkmeta")).write_bytes(metadata)
        proof = dict(index_sha256=digest(index), records=[dict(key=key, validated=True,
                     metadata_sha256=digest(metadata), gzip_sha256=digest(compressed),
                     spirv_sha256=digest(raw), runtime_buffer_slots=[8])])
        (path / "validation.json").write_text(json.dumps(proof))
        return key, raw, metadata

    def test_preinflation_only_changes_compression_flag(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)
            key, raw, meta = self.fixture(source)
            report = prepare_shaders(source, source / "ready")
            changed = (source / "ready" / (key + ".vkmeta")).read_bytes()
            self.assertEqual(changed, meta[:44] + bytes(4) + meta[48:])
            self.assertEqual((source / "ready" / (key + ".spv")).read_bytes(), raw)
            self.assertEqual(report["records"][0]["runtime_buffer_slots"], [8])

    def test_unverified_payload_and_truncated_index_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)
            key, _, _ = self.fixture(source)
            (source / (key + ".vkmeta")).write_bytes(b"modified")
            with self.assertRaisesRegex(ValueError, "validation receipt"):
                prepare_shaders(source, source / "ready")
            with self.assertRaises(ValueError):
                shader_index_keys((source / "shader_index.bin").read_bytes()[:-1])


class InstallTests(unittest.TestCase):
    def test_verification_and_exfat_overlay_fallback(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            base, pack = root / "base", root / "pack"
            (base / "data").mkdir(parents=True)
            (pack / "RomFS").mkdir(parents=True)
            for name in [".build.config", "build_manifest.psv", "00-00000000.data"]:
                (base / "data" / name).write_bytes(b"base")
            (pack / "shaders.squashfs").write_bytes(b"prepared")
            manifest = dict(schema=1, game_build="90447", source_manifest_sha256=digest(b"base"),
                            base_config_sha256=digest(b"base"),
                            files=[dict(path="shaders.squashfs", size=8, sha256=digest(b"prepared"))],
                            base_files=[dict(path="data/00-00000000.data", size=4)])
            (pack / "manifest.json").write_text(json.dumps(manifest))
            install(pack, base, True)
            target = pack / "RomFS/data/00-00000000.data"
            self.assertFalse(target.exists())
            self.assertFalse((pack / "installed.json").exists())
            install(pack, base)
            install(pack, base)
            self.assertFalse(target.exists())
            receipt = json.loads((pack / "installed.json").read_text())
            self.assertTrue(receipt["overlay_fallback"])
            self.assertEqual(receipt["base_resources_copied"], 0)
            (pack / "shaders.squashfs").write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "checksum"):
                install(pack, base)


if __name__ == "__main__":
    unittest.main()
