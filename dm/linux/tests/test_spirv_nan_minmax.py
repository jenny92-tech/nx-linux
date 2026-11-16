# INPUT: Owned SPIR-V fixtures, malformed modules and synthetic bank receipts.
# OUTPUT: Atomic opcode rewrite, pipeline publication and cache identity checks.
# POS: Host regressions; no device, original game files or saves required.
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from install_game import shader_pipeline_cache_directory
from prepare_shader_nan_minmax import prepare
from spirv_nan_minmax import POLICY, rewrite_nan_minmax


def op(code, *args):
    return [((len(args)+1)<<16)|code, *args]


def module(rows):
    words = [0x07230203, 0x10300, 0, 64, 0]+[x for row in rows for x in row]
    return struct.pack(f'<{len(words)}I', *words)


def import_op(ident, name):
    raw = name+b'\0'
    raw += bytes(-len(raw) % 4)
    return op(11, ident, *struct.unpack(f'<{len(raw)//4}I', raw))


def fixture():
    return module([import_op(1, b'GLSL.std.450'), import_op(2, b'Other'),
        op(12, 3, 4, 1, 37, 5, 6), op(12, 3, 7, 1, 40, 5, 6),
        op(12, 3, 8, 1, 43, 5, 6, 9), op(12, 3, 10, 2, 37, 5, 6),
        op(12, 3, 11, 1, 79, 5, 6), op(43, 3, 12, 37)])


def synthetic_bank(root):
    root.mkdir()
    bank = root/'shaders'
    bank.mkdir()
    key, raw = 'a'*16, fixture()
    control = bytearray(0x880)
    struct.pack_into('<3I', control, 0, 0x98761234, 1, 16)
    struct.pack_into('<I', control, 0x704, 4)
    struct.pack_into('<I', control, 0x714, 1)
    index = struct.pack('<3I', 0x494b5644, 2, 1)+key.encode()+struct.pack('<2I', 1, 4)+control+b'own1'
    meta = struct.pack('<12I', 0x4d4b5644, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0)
    for name, data in [('shader_index.bin', index), (key+'.spv', raw), (key+'.vkmeta', meta)]:
        (bank/name).write_bytes(data)
    proof = dict(complete=True, shader_report=dict(index_sha256=hashlib.sha256(index).hexdigest(),
        records=[dict(key=key, stage=1, spirv_sha256=hashlib.sha256(raw).hexdigest(),
                      metadata_sha256=hashlib.sha256(meta).hexdigest())]))
    (root/'complete.json').write_text(json.dumps(proof))
    return key, raw, meta, index


class NanMinmaxTests(unittest.TestCase):
    def test_only_glsl_extended_opcode_words_change(self):
        raw = fixture()
        updated, counts = rewrite_nan_minmax(raw)
        self.assertEqual(counts, dict(FMin=1, FMax=1, FClamp=1))
        old = struct.unpack(f'<{len(raw)//4}I', raw)
        new = struct.unpack(f'<{len(updated)//4}I', updated)
        self.assertEqual([(a, b) for a, b in zip(old, new) if a != b], [(37, 79), (40, 80), (43, 81)])
        self.assertEqual(rewrite_nan_minmax(updated), (updated, dict(FMin=0, FMax=0, FClamp=0)))

    def test_multiple_imports_and_import_order_are_resolved(self):
        raw = module([op(12, 3, 4, 12, 37, 5, 6), import_op(12, b'GLSL.std.450'),
                      import_op(1, b'GLSL.std.450'), op(12, 3, 7, 1, 40, 5, 6)])
        self.assertEqual(rewrite_nan_minmax(raw)[1], dict(FMin=1, FMax=1, FClamp=0))

    def test_similarly_named_extensions_and_literal_values_stay_unchanged(self):
        for name in (b'Other', b'GLSL.std.450x'):
            raw = module([import_op(1, name), op(12, 3, 4, 1, 37, 5, 6), op(43, 3, 9, 40)])
            self.assertEqual(rewrite_nan_minmax(raw), (raw, dict(FMin=0, FMax=0, FClamp=0)))

    def test_malformed_tail_cannot_publish_earlier_changes(self):
        raw = bytearray(fixture()+struct.pack('<I', 0))
        before = bytes(raw)
        with self.assertRaises(ValueError):
            rewrite_nan_minmax(raw)
        self.assertEqual(bytes(raw), before)

    def test_bad_headers_imports_instruction_bounds_and_arities_are_rejected(self):
        bad = [b'', bytes(20), fixture()[:-1], fixture()+struct.pack('<I', 4<<16),
            module([op(11, 0, 0)]), module([op(11, 64, 0)]),
            module([op(11, 1, 0x41414141)]),
            module([import_op(1, b'Other'), import_op(1, b'GLSL.std.450')]),
            module([op(12, 3, 4, 0, 37, 5, 6)]),
            module([import_op(1, b'GLSL.std.450'), op(12, 3, 4, 1, 37, 5)]),
            module([import_op(1, b'GLSL.std.450'), op(12, 3, 4, 1, 43, 5, 6)])]
        for raw in bad:
            with self.subTest(raw=raw):
                with self.assertRaises(ValueError):
                    rewrite_nan_minmax(raw)

    def test_random_mutations_do_not_mutate_caller_memory(self):
        rng = random.Random(0x629ab135)
        for _ in range(500):
            raw = bytearray(fixture())
            raw[rng.randrange(len(raw))] = rng.randrange(256)
            raw = raw[:rng.randrange(len(raw)+1)]
            before = bytes(raw)
            try:
                rewrite_nan_minmax(raw)
            except ValueError:
                pass
            self.assertEqual(bytes(raw), before)


class NanBankTests(unittest.TestCase):
    def test_validated_bank_preserves_index_metadata_and_original_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            key, raw, meta, index = synthetic_bank(root/'source')
            tool = root/'tool'
            tool.write_bytes(b'owned tool fixture')
            calls = []

            def run(args, **kwargs):
                calls.append(args)
                if '-noappend' in args:
                    Path(args[2]).write_bytes(b'hsqs owned output')
                return subprocess.CompletedProcess(args, 0, '', '')

            with patch('prepare_shader_nan_minmax.subprocess.run', side_effect=run):
                report = prepare(root/'source', root/'out', tool, tool)
            self.assertEqual(report['nan_minmax_policy'], POLICY)
            self.assertEqual(report['nan_minmax_modules'], 1)
            self.assertEqual(report['nan_minmax_counts'], dict(FMin=1, FMax=1, FClamp=1))
            self.assertEqual((root/'source/shaders'/(key+'.spv')).read_bytes(), raw)
            self.assertEqual((root/'out/shaders'/(key+'.vkmeta')).read_bytes(), meta)
            self.assertEqual((root/'out/shaders/shader_index.bin').read_bytes(), index)
            self.assertEqual((root/'out/shaders'/(key+'.spv')).read_bytes(), rewrite_nan_minmax(raw)[0])
            self.assertTrue(any('--target-env' in a for a in calls))
            self.assertIs(report['runtime_rendering_verified'], False)

    def test_validator_failure_leaves_no_complete_receipt(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            key, raw, _, _ = synthetic_bank(root/'source')
            with patch('prepare_shader_nan_minmax.subprocess.run',
                       return_value=subprocess.CompletedProcess([], 1, '', 'owned failure')):
                with self.assertRaisesRegex(ValueError, 'validation failed'):
                    prepare(root/'source', root/'out', root/'tool', root/'tool')
            self.assertFalse((root/'out/complete.json').exists())
            self.assertFalse((root/'out/shaders.squashfs').exists())
            self.assertEqual((root/'source/shaders'/(key+'.spv')).read_bytes(), raw)

    def test_changed_source_hash_and_display_order_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            key, _, _, _ = synthetic_bank(root/'source')
            p = root/'source/complete.json'
            proof = json.loads(p.read_text())
            proof['classic_display_policy'] = 'owned display fixture'
            p.write_text(json.dumps(proof))
            with self.assertRaisesRegex(ValueError, 'before NaN/display'):
                prepare(root/'source', root/'bad-order', root/'tool', root/'tool')
            self.assertFalse((root/'bad-order').exists())
            del proof['classic_display_policy']
            p.write_text(json.dumps(proof))
            (root/'source/shaders'/(key+'.spv')).write_bytes(b'tampered')
            with self.assertRaisesRegex(ValueError, 'provenance'):
                prepare(root/'source', root/'tampered', root/'tool', root/'tool')
            self.assertFalse((root/'tampered/complete.json').exists())

    def test_shader_image_bytes_determine_driver_cache_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            image = root/'shaders.squashfs'
            image.write_bytes(b'original bank')
            first = shader_pipeline_cache_directory(image)
            self.assertEqual(first.name, hashlib.sha256(b'original bank').hexdigest())
            image.write_bytes(b'NaN compatibility bank')
            second = shader_pipeline_cache_directory(image)
            self.assertNotEqual(first, second)
            self.assertEqual(second, shader_pipeline_cache_directory(image))
            self.assertEqual(shader_pipeline_cache_directory(image, root/'writable-cache'),
                             root/'writable-cache'/second.name)
            link = root/'linked-image'
            link.symlink_to(image)
            with self.assertRaises(ValueError):
                shader_pipeline_cache_directory(link)
            self.assertFalse((root/'pipeline-cache').exists())


if __name__ == '__main__':
    unittest.main()
