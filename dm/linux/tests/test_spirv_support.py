# INPUT: Synthetic graphics support UBOs with static/dynamic/volatile loads.
# OUTPUT: Exact scale constants and strict descriptor/backing bounds checks.
# POS: Host-only tests; no proprietary shader or game data.
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from spirv_support_constants import bake_linux_support


def fixture(index=1, dynamic=False, volatile=False, member_offset=192, kind=1, stage=1):
    def op(code, *args): return [((len(args)+1)<<16)|code, *args]
    rows=[op(71,7,34,stage),op(71,7,33,0),op(71,4,6,16),op(72,5,5,35,member_offset),
          op(21,1,32,0),op(22,2,32),op(43,1,3,73),op(28,4,2,3),
          op(30,5,1,1,1,1,1,4,1,1),op(32,6,2,5),op(32,8,2,2),op(59,6,7,2),
          op(43,1,9,5),op(43,1,10,index),op(19,13),op(33,14,13),op(54,13,15,0,14),op(248,16)]
    rows += [op(65,8,11,7,9,29 if dynamic else 10),op(61,2,12,11,*([1] if volatile else [])),op(253),op(56)]
    words=[0x07230203,0x10300,0,30,0]+[v for row in rows for v in row]
    raw=struct.pack(f'<{len(words)}I',*words)
    meta=struct.pack('<12I',0x4d4b5644,1,stage,1,0,0,0,1,1,1,1,0)+struct.pack('<8i',stage,0,6,kind,0,0,1,0)
    return raw,meta


class SupportConstantTests(unittest.TestCase):
    def test_first_and_last_scale_have_exact_float_one(self):
        for index in (0,1,72):
            raw,meta=fixture(index=index);out,n=bake_linux_support(raw,meta)
            self.assertEqual(n,1)
            w=struct.unpack(f'<{len(out)//4}I',out);constants={};copies={};at=5
            while at<len(w):
                size,code=w[at]>>16,w[at]&65535;p=w[at+1:at+size]
                if code==43:constants[p[1]]=p[2]
                if code==83:copies[p[1]]=p[2]
                at+=size
            self.assertEqual(constants[copies[12]],0x3f800000)

    def test_dynamic_volatile_other_bank_and_out_of_bounds_remain(self):
        for kwargs in ({'dynamic':True},{'volatile':True},{'kind':2},{'stage':0},{'index':73},{'member_offset':1392}):
            raw,meta=fixture(**kwargs);self.assertEqual(bake_linux_support(raw,meta),(raw,0))

    def test_missing_metadata_is_rejected(self):
        raw,meta=fixture()
        with self.assertRaises(ValueError):bake_linux_support(raw,meta[:-1])

    def test_vertex_folding_requires_explicit_opt_in(self):
        raw,meta=fixture(stage=0)
        self.assertEqual(bake_linux_support(raw,meta),(raw,0))
        out,n=bake_linux_support(raw,meta,include_vertex=True)
        self.assertEqual(n,1)
        # Vertex support lives in descriptor set 0, fragment support in set 1.
        self.assertNotEqual(out,raw)
        self.assertIn(struct.pack('<I',0x3f800000),out)

    def test_vertex_cannot_fold_a_fragment_bank_by_mistake(self):
        raw,meta=fixture(stage=0)
        wrong=bytearray(meta);struct.pack_into('<i',wrong,48,1)
        self.assertEqual(bake_linux_support(raw,wrong,include_vertex=True),(raw,0))

    def test_vertex_dynamic_volatile_and_unrelated_bindings_stay_live(self):
        for kwargs in ({'dynamic':True},{'volatile':True},{'kind':2},{'index':73},{'member_offset':1392}):
            raw,meta=fixture(stage=0,**kwargs)
            self.assertEqual(bake_linux_support(raw,meta,include_vertex=True),(raw,0))

    def test_compute_is_not_folded_by_graphics_opt_in(self):
        raw,meta=fixture(stage=2)
        self.assertEqual(bake_linux_support(raw,meta,include_vertex=True),(raw,0))


if __name__=='__main__':unittest.main()
