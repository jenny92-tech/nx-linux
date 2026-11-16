"""Synthetic AArch64 instructions and signed table offsets; no game binaries."""
import struct
import unittest
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from relative_jump import relative_table


class RelativeJumpTests(unittest.TestCase):
    def fixture(self, edits=None, targets=(0x200, 0x220, 0x240), interiors=(), ro_end=0x5000,
                companion_targets=(0x270,0x280), omit_write_reporting=False):
        image = bytearray(0x5000)
        for at in range(0x100, 0x284, 4):
            struct.pack_into('<I', image, at, 0xd503201f)
        words = {
            0x104: 0x9000002b,  # adrp x11, 0x4000
            0x11c: 0x91000000 | (0x100 << 10) | (11 << 5) | 11,
            0x138: 0x53000000 | (25 << 16) | (31 << 10) | (15 << 5) | 18,
            0x13c: 0x53000000 | (1 << 16) | (24 << 10) | (15 << 5) | 16,
            0x140: 0x120001ef,  # and w15,w15,#1 (independent work after the shift)
            0x144: 0x7100001f | (2 << 10) | (18 << 5),
            0x148: 0x54000008 | (((0x280-0x148)//4) << 5),
            0x14c: 0xb8b27972,  # ldrsw x18,[x11,x18,lsl#2]
            0x150: 0x8b0b0252,  # add x18,x18,x11
            0x154: 0xd61f0240,
            0x200: 0xd65f03c0, 0x220: 0xd65f03c0,
            0x240: 0xd65f03c0, 0x280: 0xd65f03c0,
        }
        words.update(edits or {})
        for at, word in words.items():
            struct.pack_into('<I', image, at, word)
        for i, target in enumerate(targets):
            struct.pack_into('<i', image, 0x4100 + 4*i, target - 0x4100)
        struct.pack_into('<ii', image, 0x4200, *(t-0x4200 for t in companion_targets))
        decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
        decoder.detail = True
        instructions = list(decoder.disasm(bytes(image[0x100:0x284]), 0x100))
        if omit_write_reporting:
            for ins in instructions:
                ins.regs_access=lambda: ([],[])
        site = next(i for i, ins in enumerate(instructions) if ins.address == 0x154)
        return relative_table(instructions, site, image, 0x1000, 0x4000, ro_end, set(interiors))

    def test_negative_offsets_and_interleaved_instructions(self):
        self.assertEqual(self.fixture(), (0x200, 17))

    def test_loop_keeps_the_proven_base(self):
        branch = 0x14000000 | (((0x138-0x220)//4) & 0x3ffffff)
        self.assertEqual(self.fixture(edits={0x220: branch}), (0x200, 17))

    def test_path_bypassing_base_definition_is_refused(self):
        cbz = 0x34000000 | (((0x138-0x100)//4) << 5)
        self.assertEqual(self.fixture(edits={0x100: cbz}), (None, None))

    def test_base_clobber_is_refused(self):
        self.assertEqual(self.fixture(edits={0x130: 0x2a0003eb}), (None, None))

    def test_index_clobber_is_refused(self):
        self.assertEqual(self.fixture(edits={0x140: 0x2a0003f2}), (None, None))

    def test_x_index_cannot_be_bounded_by_w_compare(self):
        shift = 0xd3400000 | (25 << 16) | (63 << 10) | (15 << 5) | 18
        self.assertEqual(self.fixture(edits={0x138: shift}), (None, None))

    def test_wrong_guard_is_refused(self):
        branch = 0x54000009 | (((0x280-0x148)//4) << 5)
        self.assertEqual(self.fixture(edits={0x148: branch}), (None, None))

    def test_branch_into_guarded_dispatch_is_refused(self):
        branch = 0x14000000 | (((0x14c-0x200)//4) & 0x3ffffff)
        self.assertEqual(self.fixture(edits={0x200: branch}), (None, None))

    def test_unaligned_or_nontext_targets_are_refused(self):
        for target in (0x201, -4, 0x1000, 0x4100):
            with self.subTest(target=target):
                self.assertEqual(self.fixture(targets=(0x200, target, 0x240)), (None, None))

    def test_read_only_range_covers_the_entire_table(self):
        self.assertEqual(self.fixture(ro_end=0x4108), (None, None))

    def test_target_inside_outlined_window_is_refused(self):
        self.assertEqual(self.fixture(interiors=(0x220,)), (None, None))

    def companion(self):
        return {
            0x240:0x52800009,  # mov w9,#0
            0x244:0x7100001f | (1 << 10) | (9 << 5),
            0x248:0x54000008 | (((0x280-0x248)//4) << 5),
            0x24c:0x9000002c,
            0x250:0x91000000 | (0x200 << 10) | (12 << 5) | 12,
            0x254:0xb8a07800 | (9 << 16) | (12 << 5) | 9,
            0x258:0x8b000000 | (12 << 16) | (9 << 5) | 9,
            0x25c:0x2a1f03ea,
            0x260:0xd61f0000 | (9 << 5),
            0x270:0xd65f03c0,
        }

    def test_known_companion_dispatch_contributes_cfg_edges(self):
        self.assertEqual(self.fixture(edits=self.companion()), (0x200,17))

    def test_unknown_companion_dispatch_is_refused(self):
        edits=self.companion();edits[0x260]=0xd61f0140  # br x10 has no proven definition
        self.assertEqual(self.fixture(edits=edits), (None,None))

    def test_companion_index_must_be_zero_extended(self):
        edits=self.companion();edits[0x240]=0xaa0003e9  # mov x9,x0
        self.assertEqual(self.fixture(edits=edits), (None,None))

    def test_branch_bypassing_companion_guard_is_refused(self):
        edits=self.companion();edits[0x200]=0x14000000 | ((0x250-0x200)//4)
        self.assertEqual(self.fixture(edits=edits), (None,None))

    def test_companion_base_clobber_between_load_and_add_is_refused(self):
        edits=self.companion()
        edits[0x25c]=edits[0x258]
        edits[0x258]=0xaa0003ec  # mov x12,x0 destroys the table base after load
        self.assertEqual(self.fixture(edits=edits), (None,None))

    def test_shifted_compare_bounds_are_refused(self):
        self.assertEqual(self.fixture(edits={0x144:0x71400a5f}), (None,None))
        edits=self.companion();edits[0x244]|=1<<22
        self.assertEqual(self.fixture(edits=edits), (None,None))

    def test_companion_cannot_target_main_guarded_region(self):
        self.assertEqual(self.fixture(edits=self.companion(),companion_targets=(0x14c,0x280)),
                         (None,None))

    def test_main_cannot_target_companion_guarded_region(self):
        self.assertEqual(self.fixture(edits=self.companion(),targets=(0x200,0x250,0x240)),
                         (None,None))

    def test_pair_load_second_destination_clobbers_base(self):
        self.assertEqual(self.fixture(edits={0x130:0xa9402c09},omit_write_reporting=True),
                         (None,None))


if __name__ == '__main__':
    unittest.main()
