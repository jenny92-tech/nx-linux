# INPUT: Synthetic opaque fragments with sampled and unsampled output paths.
# OUTPUT: Dominance-safe RGB replacement, unchanged alpha/fallbacks and refusals.
# POS: Host-only shader quality checks; GPU validation is a separate requirement.
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from spirv_model_lighting import albedo_lighting
from prepare_model_lighting import trim_bindings


def op(code, *args):return [((len(args)+1)<<16)|code, *args]


def fixture(branch=False, joined_store=False, discard=False):
    rows=[op(15,4,20,0x6e69616d,0,12),op(71,11,34,1),op(71,11,33,8),op(71,12,30,0),
          op(19,1),op(22,2,32),op(23,3,2,4),op(32,4,3,3),op(32,5,3,2),op(21,6,32,1),
          op(25,8,2,1,0,0,0,1,0),op(27,7,8),op(32,9,0,7),op(33,10,1),
          op(59,9,11,0),op(59,4,12,3),*[op(43,6,13+i,i) for i in range(4)],
          op(43,2,17,0x3f800000),op(20,18),op(41,18,19),op(23,24,2,2),op(44,24,25,17,17),
          op(54,1,20,0,10),op(248,21)]
    if branch:rows += [op(247,47,0),op(250,19,45,46),op(248,45)]
    rows += [op(61,7,22,11),op(87,3,27,22,25)]
    def stores(start):
        return [r for c in range(4) for r in (op(65,5,start+c,12,13+c),op(62,start+c,17))]
    if not joined_store:rows += stores(30)
    if branch:
        rows += [op(249,47),op(248,46)]
        rows += [op(252)] if discard else [*stores(40),op(249,47)]
        rows += [op(248,47)]
    if joined_store:rows += stores(50)
    rows += [op(253),op(56)]
    words=[0x07230203,0x10300,0,100,0]+[v for row in rows for v in row]
    meta=struct.pack('<12I',0x4d4b5644,1,1,1,0,0,0,1,1,1,15,0)+struct.pack('<8i',1,8,1,4,10,-1,1,3)
    return struct.pack(f'<{len(words)}I',*words),meta


def instructions(raw):
    words=struct.unpack(f'<{len(raw)//4}I',raw);at=5;rows=[]
    while at<len(words):
        n=words[at]>>16;rows.append(words[at:at+n]);at+=n
    return rows


class ModelLightingTests(unittest.TestCase):
    def test_rgb_uses_original_sample_but_alpha_and_sample_instruction_stay(self):
        raw,meta=fixture();out,proof=albedo_lighting(raw,meta)
        rows=instructions(out);stores={r[1]:r[2] for r in rows if r[0]&65535==62}
        extracts={r[2]:r[3:] for r in rows if r[0]&65535==81}
        self.assertEqual([extracts[stores[30+i]] for i in range(3)],[(27,0),(27,1),(27,2)])
        self.assertEqual(stores[33],17)
        self.assertIn(tuple(op(87,3,27,22,25)),rows)
        self.assertEqual(proof['rgb_stores_replaced'],3)

    def test_unsampled_fallback_output_is_not_rewritten(self):
        raw,meta=fixture(branch=True);out,proof=albedo_lighting(raw,meta)
        stores={r[1]:r[2] for r in instructions(out) if r[0]&65535==62}
        self.assertEqual([stores[40+i] for i in range(4)],[17]*4)
        self.assertEqual(stores[33],17)
        self.assertEqual(proof['fallback_rgb_stores_preserved'],3)

    def test_sample_in_one_branch_cannot_replace_joined_output(self):
        raw,meta=fixture(branch=True,joined_store=True)
        with self.assertRaisesRegex(ValueError,'does not dominate'):albedo_lighting(raw,meta)

    def test_writable_metadata_and_external_writes_are_rejected(self):
        raw,meta=fixture();wrong=bytearray(meta);struct.pack_into('<i',wrong,48+12,6)
        with self.assertRaisesRegex(ValueError,'writable'):albedo_lighting(raw,wrong)
        words=list(struct.unpack(f'<{len(raw)//4}I',raw));words[-2:-2]=op(99,11,25,27)
        with self.assertRaisesRegex(ValueError,'external writes'):albedo_lighting(struct.pack(f'<{len(words)}I',*words),meta)

    def test_alpha_test_discard_and_its_branch_condition_stay(self):
        raw,meta=fixture(branch=True,discard=True);out,proof=albedo_lighting(raw,meta)
        rows=instructions(out)
        self.assertIn(tuple(op(252)),rows)
        self.assertIn(tuple(op(250,19,45,46)),rows)
        self.assertIn(tuple(op(62,33,17)),rows)
        self.assertEqual(proof['discard_operations_preserved'],1)

    def test_another_output_or_nonfragment_cannot_be_replaced(self):
        raw,meta=fixture();words=list(struct.unpack(f'<{len(raw)//4}I',raw));words[5:5]=op(59,4,90,3)
        with self.assertRaisesRegex(ValueError,'one location-0'):albedo_lighting(struct.pack(f'<{len(words)}I',*words),meta)
        wrong=bytearray(meta);struct.pack_into('<I',wrong,8,0)
        with self.assertRaisesRegex(ValueError,'fragment metadata'):albedo_lighting(raw,wrong)

    def test_removed_binding_keeps_other_binding_guards_and_constant_tail(self):
        first=struct.pack('<8i',1,8,1,4,10,-1,1,3)
        removed=struct.pack('<8i',1,9,6,7,1,0,1,0)
        tail=struct.pack('<2I',4,0)+struct.pack('<I',1)+b'ABCD'
        meta=struct.pack('<12I',0x4d4b5644,1,1,2,1,4,1,1,1,1,15,0)+first+removed+tail
        out=trim_bindings(meta,{(1,8)})
        self.assertEqual(struct.unpack_from('<I',out,12)[0],1)
        self.assertEqual(out[48:],first+tail)
        with self.assertRaisesRegex(ValueError,'introduced'):trim_bindings(meta,{(1,99)})


if __name__=='__main__':unittest.main()
