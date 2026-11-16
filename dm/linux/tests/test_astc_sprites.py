# INPUT: Synthetic packaged SpA1 BC3/RGBA/ASTC4 atlases and local astcenc.
# OUTPUT: Strict threshold, alpha/spatial, animation and native layout proofs.
# POS: Host-only sprite preparation tests; no proprietary assets or device use.
import errno
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from handheld_mips import Descriptor, NativeLayout
from prepare_astc_blocks import ASTC_MAGIC, encode_blocks, pixels_to_blocks
from prepare_astc_sprites import (NVN_FORMATS, decode_bc3, prepare, read_replacements,
                                  read_sprite, rebuild_sprite, sprite_descriptor)

ROOT = Path(__file__).resolve().parents[3]
ASTCENC = ROOT / 'dm/linux/out/tools/astcenc-5.2.0/bin/astcenc'


@unittest.skipUnless(ASTCENC.is_file(), 'local astcenc required; no dependency download')
class SpriteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        cls.native = NativeLayout(cls.root / 'native')
        cls.fixtures = {}
        for fmt in (31, 32, 61, 62, 95, 81):
            cls.fixtures[fmt] = cls.fixture(fmt, 257, 7)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @classmethod
    def fixture(cls, fmt, width, height):
        y, x = np.indices((height, width))
        rgba = np.stack(((x // 8 * 17 + 23) % 256, (y // 8 * 31 + 41) % 256,
                         np.full((height, width), 77), (x // 8 * 11 + 17) % 255), axis=2).astype(np.uint8)
        if fmt in (31, 32):
            linear = rgba.tobytes()
        elif fmt in (61, 62):
            blocks = bytearray()
            for by in range((height + 3) // 4):
                for bx in range((width + 3) // 4):
                    red, green, blue = (bx // 2 * 3 + 5) % 32, (by // 2 * 5 + 23) % 64, 11
                    c0 = (red << 11) | (green << 5) | blue
                    alpha = (bx // 2 * 11 + 17) % 255
                    blocks.extend(bytes((alpha, 0)) + bytes(6) + struct.pack('<HHI', c0, 0, 0))
            linear = bytes(blocks)
            rgba = decode_bc3(linear, width, height)
        else:
            padded = np.pad(rgba, ((0, (-height) % 4), (0, (-width) % 4), (0, 0)), mode='edge')
            count = ((width + 3) // 4) * ((height + 3) // 4)
            linear = encode_blocks(pixels_to_blocks(padded, 4, count), 4, fmt == 81, ASTCENC, 'fastest', cls.root)
        header = bytearray(32)
        struct.pack_into('<4sHHIIhhH', header, 0, b'SpA1', fmt, 63, width, height, -19, 27, 4)
        header[22] = 3
        header[23:31] = bytes((0, 0, 0, 0, 7, 0, 1, 0))
        header[31] = 0x91
        desc = Descriptor(1, NVN_FORMATS[fmt], 0, 0, width, height, 1, 1, 0, 1)
        storage = cls.native.swizzle(desc, [linear])
        return bytes(header) + struct.pack('<Ii', len(storage), 4) + storage, rgba

    def work(self, root, fixtures):
        work = root / 'source'
        paths = {}
        for name, raw in fixtures.items():
            source = work / 'files' / name
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(raw)
            paths[name] = source
        with (work / 'repl.tsv').open('w') as stream:
            for name, source in paths.items():
                stream.write(name + '\t' + str(source) + '\n')
        return work, paths

    def decode8(self, linear, info, root):
        width, height = info['atlas_width'], info['height']
        header = ASTC_MAGIC + bytes((8, 8, 1))
        header += width.to_bytes(3, 'little') + height.to_bytes(3, 'little') + bytes((1, 0, 0))
        source, output = root / 'decoded.astc', root / 'decoded.tga'
        source.write_bytes(header + linear)
        subprocess.run([str(ASTCENC), '-ds' if info['format'] == 88 else '-dl',
                        str(source), str(output), '-silent'], check=True)
        with Image.open(output) as image:
            return np.array(image.convert('RGBA'))

    def test_all_supported_sources_nonpot_alpha_animation_and_native_roundtrip(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            fixtures = {f'data/hd/ui/{fmt}.sprite': value[0] for fmt, value in self.fixtures.items()}
            work, sources = self.work(root, fixtures)
            report = prepare(work, root / 'target', ASTCENC, preset='fastest', batch_mib=1)
            self.assertEqual(report['changed'], 6)
            # BC3 and RGBA share each profile batch; ASTC4 uses a decode batch.
            self.assertEqual(report['codec_batches'], 4)
            outputs = read_replacements(root / 'target')
            for fmt, (original, expected) in self.fixtures.items():
                name = f'data/hd/ui/{fmt}.sprite'
                raw = outputs[name].read_bytes()
                self.assertEqual(raw[6:23], original[6:23])
                self.assertEqual(raw[24:32], original[24:32])
                info, desc, storage = read_sprite(raw, self.native)
                self.assertEqual(info['format'], 88 if fmt in (32, 62, 81) else 102)
                self.assertEqual((info['atlas_width'], info['height'], info['frames'], info['frame_width']), (257, 7, 4, 63))
                self.assertEqual((info['origin_x'], info['origin_y']), (-19, 27))
                linear8 = self.native.linear(desc, 0, storage)
                self.assertEqual(len(linear8), 33 * 16)
                self.assertEqual(self.native.swizzle(desc, [linear8]), storage)
                automatic = sprite_descriptor(info, 0)
                automatic.pkg_block_h = 0
                self.assertEqual(raw[23], self.native.layout(automatic).tileH)
                decoded = self.decode8(linear8, info, root)
                self.assertEqual(decoded.shape, (7, 257, 4))
                self.assertLessEqual(int(np.abs(decoded.astype(int) - expected.astype(int)).max()), 5)
                self.assertLess(int(decoded[:, :, 3].min()), 255)
                self.assertGreater(len(np.unique(decoded[:, :, 3])), 4)
                self.assertEqual(sources[name].read_bytes(), original)
            self.assertTrue(all(record['native_roundtrip_verified'] for record in report['records']))
            self.assertTrue((root / 'target/manifest.json').is_file())

    def test_strict_atlas_threshold_not_frame_width_and_unsupported_preservation(self):
        small, _ = self.fixture(31, 128, 5)
        larger, _ = self.fixture(31, 129, 5)
        unknown = bytearray(small)
        struct.pack_into('<H', unknown, 4, 50)
        # Unknown native formats are retained, without claiming their layout.
        struct.pack_into('<I', unknown, 8, 300)
        fixtures = {'data/hd/ui/small.sprite': small, 'data/hd/ui/large.sprite': larger,
                    'data/hd/ui/unknown.sprite': bytes(unknown)}
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            work, sources = self.work(root, fixtures)
            report = prepare(work, root / 'target', ASTCENC, preset='fastest')
            self.assertEqual(report['changed'], 1)
            self.assertEqual(report['preserved'], {'at or below threshold': 1, 'unsupported format 50': 1})
            outputs = read_replacements(root / 'target')
            for name in ('data/hd/ui/small.sprite', 'data/hd/ui/unknown.sprite'):
                self.assertEqual(outputs[name].read_bytes(), fixtures[name])
                self.assertEqual(outputs[name].stat().st_ino, sources[name].stat().st_ino)
            unknown_report = next(record for record in report['records'] if record['source_format'] == 50)
            self.assertFalse(unknown_report['source_native_layout_verified'])
            self.assertFalse(unknown_report['changed'])
            large_info, _, _ = read_sprite(outputs['data/hd/ui/large.sprite'].read_bytes(), self.native)
            self.assertEqual((large_info['atlas_width'], large_info['frame_width']), (129, 63))

    def test_corrupt_header_geometry_pointer_layout_and_storage_are_rejected(self):
        raw = self.fixtures[61][0]
        corruptions = []
        bad = bytearray(raw); bad[:4] = b'BAD!'; corruptions.append(bad)
        bad = bytearray(raw); struct.pack_into('<I', bad, 8, 0); corruptions.append(bad)
        bad = bytearray(raw); struct.pack_into('<H', bad, 20, 0); corruptions.append(bad)
        bad = bytearray(raw); bad[22] = 0; corruptions.append(bad)
        bad = bytearray(raw); bad[23] = 6; corruptions.append(bad)
        bad = bytearray(raw); struct.pack_into('<i', bad, 36, -4); corruptions.append(bad)
        bad = bytearray(raw); struct.pack_into('<I', bad, 32, 1); corruptions.append(bad)
        tall, _ = self.fixture(61, 257, 257)
        bad = bytearray(tall); bad[23] = 5; corruptions.append(bad)
        corruptions.extend((raw[:-1], raw + b'x'))
        for bad in corruptions:
            with self.subTest(header=bytes(bad[:40]).hex()):
                with self.assertRaises(ValueError):
                    read_sprite(bad, self.native)
        with self.assertRaisesRegex(ValueError, 'linear mip size'):
            rebuild_sprite(raw, bytes(16), self.native)

    def test_budget_batches_copy_fallback_and_output_alias_rejection(self):
        raw, _ = self.fixture(31, 257, 257)
        small, _ = self.fixture(31, 128, 5)
        fixtures = {f'data/hd/ui/batch-{i}.sprite': raw for i in range(5)}
        fixtures['data/hd/ui/small.sprite'] = small
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            work, sources = self.work(root, fixtures)
            with self.assertRaisesRegex(ValueError, 'separate'):
                prepare(work, work, ASTCENC)
            with self.assertRaisesRegex(ValueError, 'separate'):
                prepare(work, work / 'child', ASTCENC)
            with mock.patch('prepare_astc_sprites.os.link', side_effect=OSError(errno.EXDEV, 'cross-device')):
                report = prepare(work, root / 'target', ASTCENC, preset='fastest', batch_mib=1)
            self.assertEqual(report['changed'], 5)
            self.assertEqual(report['codec_batches'], 2)
            self.assertEqual(report['unchanged_files_materialized'], {'copied': 1})
            copied = root / 'target/files/data/hd/ui/small.sprite'
            self.assertEqual(copied.read_bytes(), small)
            self.assertNotEqual(copied.stat().st_ino, sources['data/hd/ui/small.sprite'].stat().st_ino)
            for name, source in sources.items():
                self.assertEqual(source.read_bytes(), fixtures[name])
            with self.assertRaisesRegex(ValueError, 'already exists'):
                prepare(work, root / 'target', ASTCENC)


if __name__ == '__main__':
    unittest.main()
