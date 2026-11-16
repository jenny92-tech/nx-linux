# INPUT: Synthetic ASTC4 textures, local astcenc and renderer NVN layout code.
# OUTPUT: Threshold, spatial reconstruction, native mip and source proofs.
# POS: Host-only ASTC8 preparation tests; no game assets or device access.
import base64
import copy
import errno
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from handheld_mips import NativeLayout, descriptor, read_texture, trim_texture, validate_thumbnail
from prepare_astc_blocks import (ASTC_MAGIC, CACHE_PATH, astc_file, encode_blocks,
                                pixels_to_blocks, prepare, read_replacements, rebuild_texture,
                                transcode_batch)

ROOT = Path(__file__).resolve().parents[3]
ASTCENC = ROOT / 'dm/linux/out/tools/astcenc-5.2.0/bin/astcenc'


@unittest.skipUnless(ASTCENC.is_file(), 'local astcenc is required; no download is performed')
class AstcBlocksTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        cls.native = NativeLayout(cls.root / 'native')
        cls.fixtures = {}
        for fmt, width, height in ((95, 256, 128), (81, 256, 256), (95, 128, 128)):
            cls.fixtures[(fmt, width, height)] = cls.fixture(fmt, width, height)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @classmethod
    def fixture(cls, fmt, width, height):
        levels = max(width, height).bit_length()
        entry = dict(filename=f'data/hd/ui/sample-{fmt}-{width}.texture', type=2,
                     format=fmt, width=width, height=height, depth=1, levels=levels,
                     layoutType=3, layoutData=0x1000700000003, thumbnailMip=levels - 4)
        images, blocks, lengths = [], [], []
        for level in range(levels):
            w, h = max(1, width >> level), max(1, height >> level)
            y, x = np.indices((h, w))
            image = np.stack(((x // 8 * 17 + level * 19) % 256,
                              (y // 8 * 23 + 37) % 256,
                              np.full((h, w), 73 + level),
                              (x // 8 * 11 + y // 8 * 7 + 41 + level * 13) % 256), axis=2).astype(np.uint8)
            images.append(image)
            padded = np.pad(image, ((0, (-h) % 4), (0, (-w) % 4), (0, 0)), mode='edge')
            n = ((w + 3) // 4) * ((h + 3) // 4)
            blocks.append(pixels_to_blocks(padded, 4, n))
            lengths.append(n * 16)
        encoded = encode_blocks(np.concatenate(blocks), 4, fmt == 81, ASTCENC, 'fastest', cls.root)
        mips, offset = [], 0
        for length in lengths:
            mips.append(encoded[offset:offset + length])
            offset += length
        desc = descriptor(entry)
        storage = cls.native.swizzle(desc, mips)
        header = bytearray(36)
        header[:4] = bytes.fromhex('3c444528')
        header[4], header[6], header[7], header[18] = fmt, 2, levels, 3
        struct.pack_into('<IIH', header, 8, width, height, 1)
        struct.pack_into('<Q', header, 19, entry['layoutData'])
        struct.pack_into('<II', header, 28, levels, 4)
        offsets = cls.native.offsets(desc) + [cls.native.layout(desc).size]
        start = 36 + 8 * levels
        for level in range(levels):
            header.extend(struct.pack('<Ii', offsets[level + 1] - offsets[level],
                                      start + offsets[level] - (40 + 8 * level)))
        retained = mips[entry['thumbnailMip']:]
        thumbnail = bytearray(struct.pack('<Ii', len(retained), 4))
        position = 8 + 8 * len(retained)
        for level, mip in enumerate(retained):
            thumbnail.extend(struct.pack('<Ii', len(mip), position - (12 + 8 * level)))
            position += len(mip)
        thumbnail.extend(b''.join(retained))
        entry['thumbnailBase64'] = base64.b64encode(thumbnail).decode('ascii')
        return bytes(header) + storage, entry, mips, images

    def work_fixture(self, root, fixtures=None):
        work = root / 'source'
        replacements, entries = {}, []
        fixtures = fixtures or list(self.fixtures.values())
        for raw, entry, _, _ in fixtures:
            source = work / 'files' / entry['filename']
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(raw)
            replacements[entry['filename']] = source
            entries.append(copy.deepcopy(entry))
        cache = work / 'files' / CACHE_PATH
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_text(json.dumps(entries))
        replacements[CACHE_PATH] = cache
        with (work / 'repl.tsv').open('w') as stream:
            for name, source in replacements.items():
                stream.write(name + '\t' + str(source) + '\n')
        return work, replacements

    def test_threshold_profiles_headers_thumbnails_and_sources(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            work, originals = self.work_fixture(root)
            before = {name: source.read_bytes() for name, source in originals.items()}
            report = prepare(work, root / 'target', ASTCENC, preset='fastest', batch_mib=1)
            self.assertEqual(report['changed'], 2)
            self.assertEqual(report['codec_batches'], 2)
            self.assertTrue(report['lossy_astc4_recompression'])
            self.assertFalse(report['selection_excludes_ui'])
            output = read_replacements(root / 'target')
            entries = json.loads(output[CACHE_PATH].read_text())
            for entry, fixture in zip(entries, self.fixtures.values()):
                raw, old, _, _ = fixture
                if old['width'] == 128:
                    self.assertEqual(entry, old)
                    self.assertEqual(output[entry['filename']], (root / 'target/files' / entry['filename']).resolve())
                    self.assertEqual(output[entry['filename']].stat().st_ino,
                                     originals[entry['filename']].stat().st_ino)
                    self.assertEqual(output[entry['filename']].read_bytes(), raw)
                    continue
                self.assertEqual(entry['format'], {95: 102, 81: 88}[old['format']])
                self.assertEqual((entry['width'], entry['height'], entry['levels']),
                                 (old['width'], old['height'], old['levels']))
                self.assertEqual(entry['layoutData'] & ~255, old['layoutData'] & ~255)
                target = output[entry['filename']].read_bytes()
                self.assertLess(len(target), len(raw))
                desc, storage, _ = read_texture(target, entry, self.native)
                automatic = descriptor(entry)
                automatic.pkg_block_h = 0
                self.assertEqual(entry['layoutData'] & 255, self.native.layout(automatic).tileH)
                validate_thumbnail(entry)
                thumbnail = base64.b64decode(entry['thumbnailBase64'])
                count = struct.unpack_from('<I', thumbnail)[0]
                self.assertEqual(count, entry['levels'] - entry['thumbnailMip'])
                for level in range(entry['levels']):
                    w, h = max(1, entry['width'] >> level), max(1, entry['height'] >> level)
                    mip = self.native.linear(desc, level, storage)
                    self.assertEqual(len(mip), ((w + 7) // 8) * ((h + 7) // 8) * 16)
                    if level >= entry['thumbnailMip']:
                        position = 8 + 8 * (level - entry['thumbnailMip'])
                        size, relative = struct.unpack_from('<Ii', thumbnail, position)
                        self.assertEqual(thumbnail[position + 4 + relative:position + 4 + relative + size], mip)
                # The existing mip trimmer must retain exact ASTC8 bytes too.
                trimmed, reduced, skip = trim_texture(target, entry, 128, self.native)
                self.assertEqual(skip, 1)
                small_desc, small_storage, _ = read_texture(trimmed, reduced, self.native)
                for level in range(small_desc.levels):
                    self.assertEqual(self.native.linear(small_desc, level, small_storage),
                                     self.native.linear(desc, level + 1, storage))
            for name, source in originals.items():
                self.assertEqual(source.read_bytes(), before[name])
            self.assertTrue((root / 'target/manifest.json').is_file())
            self.assertEqual(len(report['records'][0]['mip_sha256']), 9)
            self.assertEqual(report['unchanged_files_materialized'], {'hardlinked': 1})

    def decode_mip(self, data, block, w, h, srgb, root):
        header = ASTC_MAGIC + bytes((block, block, 1)) + w.to_bytes(3, 'little') + h.to_bytes(3, 'little') + bytes((1, 0, 0))
        source, output = root / 'mip.astc', root / 'mip.tga'
        source.write_bytes(header + data)
        subprocess.run([str(ASTCENC), '-ds' if srgb else '-dl', str(source), str(output), '-silent'], check=True)
        with Image.open(output) as image:
            return np.array(image.convert('RGBA'))

    def test_real_codec_spatial_mips_and_alpha_including_one_two_four_texels(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for fmt in (95, 81):
                raw, entry, mips, images = self.fixtures[(fmt, 256, 128 if fmt == 95 else 256)]
                chain = [(max(1, entry['width'] >> level), max(1, entry['height'] >> level), mip)
                         for level, mip in enumerate(mips)]
                # Multiple chains share one atlas and must not bleed across mips.
                output = transcode_batch([chain, chain], fmt == 81, ASTCENC, 'fastest', root)
                self.assertEqual(output[0], output[1])
                for (w, h, source), target, authored in zip(chain, output[0], images):
                    decoded4 = self.decode_mip(source, 4, w, h, fmt == 81, root)
                    decoded8 = self.decode_mip(target, 8, w, h, fmt == 81, root)
                    error = np.abs(decoded8.astype(int) - decoded4.astype(int))
                    self.assertLessEqual(int(error.max()), 4)
                    self.assertLessEqual(int(np.abs(decoded8[:, :, 3].astype(int) - authored[:, :, 3].astype(int)).max()), 4)
                    self.assertLess(int(decoded8[:, :, 3].min()), 255)
                result, changed = rebuild_texture(raw, entry, output[0], self.native)
                self.assertEqual(result[4], changed['format'])

    def test_invalid_metadata_and_source_aliasing_are_rejected(self):
        raw, entry, mips, _ = self.fixtures[(95, 256, 128)]
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            broken = dict(entry, height=64)
            with self.assertRaisesRegex(ValueError, 'disagree'):
                rebuild_texture(raw, broken, mips, self.native)
            non_pot = bytearray(raw)
            struct.pack_into('<I', non_pot, 8, 192)
            with self.assertRaisesRegex(ValueError, 'non-power-of-two'):
                rebuild_texture(non_pot, dict(entry, width=192), mips, self.native)
            broken = dict(entry, thumbnailBase64=base64.b64encode(b'bad').decode())
            with self.assertRaises(ValueError):
                rebuild_texture(raw, broken, mips, self.native)
            work, _ = self.work_fixture(root, [(raw[:-1], entry, mips, [])])
            with self.assertRaisesRegex(ValueError, 'payload'):
                prepare(work, root / 'invalid', ASTCENC, preset='fastest')
            with self.assertRaisesRegex(ValueError, 'separate'):
                prepare(work, work, ASTCENC)
            with self.assertRaisesRegex(ValueError, 'separate'):
                prepare(work, work / 'child', ASTCENC)
            with self.assertRaisesRegex(ValueError, 'already exists'):
                prepare(work, root / 'invalid', ASTCENC)

    def test_batch_budget_flushes_and_cross_filesystem_copy_preserves_sources(self):
        raw, entry, mips, images = self.fixtures[(95, 256, 128)]
        fixtures = [(raw, dict(entry, filename=f'data/hd/ui/batch-{i}.texture'), mips, images)
                    for i in range(7)]
        fixtures.append(self.fixtures[(95, 128, 128)])
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            work, originals = self.work_fixture(root, fixtures)
            with mock.patch('prepare_astc_blocks.os.link', side_effect=OSError(errno.EXDEV, 'cross-device')):
                report = prepare(work, root / 'target', ASTCENC, preset='fastest', batch_mib=1)
            self.assertEqual(report['changed'], 7)
            self.assertEqual(report['codec_batches'], 2)
            self.assertEqual(report['unchanged_files_materialized'], {'copied': 1})
            unchanged = self.fixtures[(95, 128, 128)][1]['filename']
            target = root / 'target/files' / unchanged
            self.assertEqual(target.read_bytes(), originals[unchanged].read_bytes())
            self.assertNotEqual(target.stat().st_ino, originals[unchanged].stat().st_ino)
            for _, source_entry, _, _ in fixtures[:-1]:
                self.assertEqual(originals[source_entry['filename']].read_bytes(), raw)

    def test_astc_atlas_rejects_partial_blocks(self):
        with self.assertRaises(ValueError):
            astc_file(bytes(15), 4)


if __name__ == '__main__':
    unittest.main()
