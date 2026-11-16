# INPUT: Synthetic 90447 object-pool initializer and allocation-growth guards.
# OUTPUT: Batch operand decoding and fail-closed, atomic patch validation.
# POS: Host-only patch contract tests; no game assets, deployment or gameplay.
from pathlib import Path
import struct
import sys
import unittest

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from prepare_classic_native import (
    prepare_render_object_batch,
    RENDER_OBJECT_POOL_CONFIG_OFFSET as CONFIG,
    RENDER_OBJECT_POOL_CONFIG_WORDS as WORDS,
    RENDER_OBJECT_POOL_GROWTH_OFFSET as GROWTH,
    RENDER_OBJECT_POOL_GROWTH_WORDS as GROWTH_WORDS,
)


class RenderObjectPoolTests(unittest.TestCase):
    def fixture(self):
        image = bytearray(GROWTH + len(GROWTH_WORDS)*4 + 16)
        struct.pack_into('<' + 'I'*len(WORDS), image, CONFIG, *WORDS)
        struct.pack_into('<' + 'I'*len(GROWTH_WORDS), image, GROWTH, *GROWTH_WORDS)
        return image

    def test_changes_only_packed_batch_and_preserves_size_and_growth(self):
        image = self.fixture(); before = bytes(image)
        report = prepare_render_object_batch(image, 64)
        at = CONFIG + 9*4
        word = struct.unpack_from('<I', image, at)[0]
        # Decode MOVK X9,#imm16,LSL#32 rather than trusting report metadata.
        self.assertEqual(word & 0xffe0001f, 0xf2c00009)
        self.assertEqual((word >> 5) & 65535, 64)
        self.assertEqual(struct.unpack_from('<I', image, at-4)[0], 0xd2836309)
        self.assertEqual(image[:at], before[:at])
        self.assertEqual(image[at+4:], before[at+4:])
        self.assertFalse(report['hard_object_limit_added'])
        self.assertFalse(report['growth_code_changed'])
        self.assertFalse(report['physical_memory_saving_verified'])

    def test_every_initializer_and_growth_word_is_guarded_before_any_write(self):
        for offset,words in ((CONFIG,WORDS),(GROWTH,GROWTH_WORDS)):
            for i in range(len(words)):
                with self.subTest(offset=offset,word=i):
                    image = self.fixture(); image[offset+i*4] ^= 1; before = bytes(image)
                    with self.assertRaises(ValueError): prepare_render_object_batch(image,64)
                    self.assertEqual(image,before)

    def test_default_invalid_short_and_repeat_inputs_are_atomic(self):
        image = self.fixture(); before = bytes(image)
        self.assertIsNone(prepare_render_object_batch(image))
        self.assertEqual(image,before)
        for value in (0,1,32,128,8192,True,64.0,'64'):
            with self.subTest(value=value):
                with self.assertRaises(ValueError): prepare_render_object_batch(image,value)
                self.assertEqual(image,before)
        for size in (0,CONFIG+40,GROWTH+16):
            short = bytearray(size); original = bytes(short)
            with self.assertRaises(ValueError): prepare_render_object_batch(short,64)
            self.assertEqual(short,original)
        prepare_render_object_batch(image,64); changed = bytes(image)
        with self.assertRaises(ValueError): prepare_render_object_batch(image,64)
        self.assertEqual(image,changed)


if __name__ == '__main__':
    unittest.main()
