# INPUT: Small shader records with known C1 addresses and exact IEEE bit values.
# OUTPUT: Immutable-load folding, padding and dynamic/volatile exclusion checks.
# POS: Host-only semantic regression checks; no game resources required.
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from spirv_c1_constants import bake_c1


def op(code, *args):
    return [((len(args)+1)<<16)|code, *args]


def fixture(index=1, component=2, volatile=False, dynamic=False):
    words = [0x07230203, 0x10300, 0, 40, 0]
    rows = [op(71, 9, 34, 1), op(71, 9, 33, 22), op(71, 5, 6, 16),
            op(72, 6, 0, 35, 0), op(21, 1, 32, 0), op(22, 2, 32), op(23, 3, 2, 4),
            op(43, 1, 4, 4), op(28, 5, 3, 4), op(30, 6, 5),
            op(32, 7, 2, 6), op(32, 8, 2, 2), op(59, 7, 9, 2),
            op(43, 1, 10, 0), op(43, 1, 11, index), op(43, 1, 12, component),
            op(19, 15), op(33, 17, 15), op(54, 15, 16, 0, 17), op(248, 18)]
    if dynamic: rows.append(op(61, 1, 19, 38))
    rows.extend([op(65, 8, 13, 9, 10, 19 if dynamic else 11, 12),
                 op(61, 2, 14, 13, *([1] if volatile else [])), op(253), op(56)])
    for row in rows: words.extend(row)
    raw = struct.pack(f'<{len(words)}I', *words)
    c1 = bytes(24) + struct.pack('<2I', 0x7fc00011, 0x80000000)
    metadata = struct.pack('<12I', 0x4d4b5644, 1, 1, 1, 0, len(c1), 0, 1, 1, 1, 1, 0)
    metadata += struct.pack('<8i', 1, 22, 6, 7, 1, 0, 1, 0) + c1
    return raw, metadata


def copied_literal(raw):
    w = struct.unpack(f'<{len(raw)//4}I', raw); constants = {}; copied = None; at = 5
    while at < len(w):
        n, code = w[at]>>16, w[at]&65535; p = w[at+1:at+n]
        if code == 43 and len(p) == 3: constants[p[1]] = p[2]
        if code == 83 and p[1] == 14: copied = p[2]
        at += n
    return constants[copied]


class ImmutableC1Tests(unittest.TestCase):
    def test_nan_payload_bits_are_preserved(self):
        raw, meta = fixture(); updated, count = bake_c1(raw, meta)
        self.assertEqual(count, 1); self.assertEqual(copied_literal(updated), 0x7fc00011)

    def test_negative_zero_bits_are_preserved(self):
        raw, meta = fixture(component=3); updated, count = bake_c1(raw, meta)
        self.assertEqual(count, 1); self.assertEqual(copied_literal(updated), 0x80000000)

    def test_renderer_zero_padding_is_preserved(self):
        raw, meta = fixture(index=3, component=3); updated, count = bake_c1(raw, meta)
        self.assertEqual(count, 1); self.assertEqual(copied_literal(updated), 0)

    def test_dynamic_and_volatile_loads_are_unchanged(self):
        for kwargs in ({'dynamic': True}, {'volatile': True}, {'index': 4}):
            raw, meta = fixture(**kwargs)
            self.assertEqual(bake_c1(raw, meta), (raw, 0))

    def test_other_descriptor_bank_is_unchanged(self):
        raw, meta = fixture(); meta = bytearray(meta); struct.pack_into('<i', meta, 48+12, 2)
        self.assertEqual(bake_c1(raw, meta), (raw, 0))


if __name__ == '__main__':
    unittest.main()
