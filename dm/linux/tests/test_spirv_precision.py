"""Precision selection must protect addresses/control/depth transitively."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from spirv_precision import relax_color_arithmetic


def instruction(op, *args):
    return [(len(args)+1) << 16 | op, *args]


def module(*extra):
    rows = [instruction(22, 1, 32), instruction(21, 2, 32, 0),
        instruction(20, 3), instruction(32, 4, 1, 1), instruction(32, 5, 3, 1),
        instruction(59, 4, 10, 1), instruction(59, 5, 90, 3),
        instruction(61, 1, 20, 10), instruction(133, 1, 21, 20, 20),
        instruction(129, 1, 22, 21, 20), *extra, instruction(62, 90, 22)]
    words = [0x07230203, 0x10000, 0, 200, 0]
    for row in rows: words.extend(row)
    return struct.pack('<%dI' % len(words), *words)


def selected(raw):
    raw, count = relax_color_arithmetic(raw)
    words = struct.unpack('<%dI' % (len(raw)//4), raw); at = 5; ids = set()
    while at < len(words):
        n = words[at] >> 16
        if words[at] & 65535 == 71 and n == 3 and words[at+2] == 0: ids.add(words[at+1])
        at += n
    assert count == len(ids)
    return ids


class PrecisionTests(unittest.TestCase):
    def test_color_arithmetic_can_be_selected(self):
        self.assertEqual(selected(module(instruction(71, 90, 30, 0))), {21, 22})

    def test_integer_bitcast_protects_ancestors(self):
        self.assertEqual(selected(module(instruction(71, 90, 30, 0), instruction(124, 2, 30, 21))), {22})

    def test_comparison_protects_ancestors(self):
        self.assertEqual(selected(module(instruction(71, 90, 30, 0), instruction(184, 3, 30, 21, 20))), {22})

    def test_texture_coordinates_keep_precision(self):
        self.assertEqual(selected(module(instruction(71, 90, 30, 0), instruction(87, 1, 30, 10, 21))), {22})

    def test_depth_and_unknown_stores_protect_every_ancestor(self):
        self.assertEqual(selected(module(instruction(71, 90, 11, 22))), set())

    def test_no_contraction_and_remaining_calls(self):
        self.assertEqual(selected(module(instruction(71, 90, 30, 0), instruction(71, 22, 42))), set())
        self.assertEqual(selected(module(instruction(71, 90, 30, 0), instruction(57, 1, 30, 80))), set())


if __name__ == '__main__': unittest.main()
