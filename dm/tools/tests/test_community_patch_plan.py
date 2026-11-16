import copy
import hashlib
import importlib.util
from pathlib import Path
import unittest
import importlib.util
import json
import os
import struct
import subprocess
import sys
import tempfile

spec = importlib.util.spec_from_file_location('community_patch_plan', Path(__file__).resolve().parents[1] / 'community_patch_plan.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CommunityPlanTests(unittest.TestCase):
    def setUp(self):
        self.nso = b'synthetic source NSO'
        self.image = bytearray(range(64))
        self.identity = dict(title_id='0100726014352000', build_id='db0507f36b84200ac05f75d76ed5026f', module='main', text_end=32)
        self.plan = {k: v for k, v in self.identity.items() if k != 'text_end'}
        self.plan.update(nso_sha256=hashlib.sha256(self.nso).hexdigest(), patches=[dict(
            id='synthetic-quality', category='graphics', enabled=True,
            writes=[dict(offset=4, expected='04050607', replace='1f2003d5')])])

    def apply(self, plan=None):
        return module.apply_plan(self.image, self.nso, plan or self.plan, **self.identity)

    def test_selected_and_disabled(self):
        p=copy.deepcopy(self.plan);p['patches'][0]['enabled']=False
        self.assertEqual(self.apply(p)['written_bytes'],0)
        self.assertEqual(self.image,bytearray(range(64)))
        r=self.apply();self.assertEqual(self.image[4:8],bytes.fromhex('1f2003d5'))
        self.assertEqual(r['selected'],['synthetic-quality'])

    def test_identity_guards(self):
        for key,value in [('title_id','0100000000000000'),('build_id','00'*16),('nso_sha256','00'*32),('module','sdk')]:
            with self.subTest(key=key):
                p=copy.deepcopy(self.plan);p[key]=value
                with self.assertRaises(ValueError):self.apply(p)
                self.assertEqual(self.image,bytearray(range(64)))

    def test_conflicts_are_transactional(self):
        for write in [dict(offset=4,expected='04050607',replace='00000000'),
                      dict(offset=12,expected='00000000',replace='00000000'),
                      dict(offset=63,expected='00000000',replace='00000000'),
                      dict(offset=3,expected='03040506',replace='00000000')]:
            p=copy.deepcopy(self.plan);p['patches'][0]['writes'].append(write)
            with self.subTest(write=write), self.assertRaises(ValueError):self.apply(p)
            self.assertEqual(self.image,bytearray(range(64)))

    def test_reject_bad_enabled_and_category(self):
        for key,value in [('enabled','false'),('category','cheat')]:
            p=copy.deepcopy(self.plan);p['patches'][0][key]=value
            with self.assertRaises(ValueError):self.apply(p)

    def test_reserved_cut_cannot_be_overridden_even_if_bytes_match(self):
        with self.assertRaisesRegex(ValueError,'reserved built-in'):
            module.apply_plan(self.image,self.nso,self.plan,reserved=[(4,4)],**self.identity)
        self.assertEqual(self.image,bytearray(range(64)))


@unittest.skipUnless(importlib.util.find_spec('capstone'),'requires the existing rewriter dependency')
class RewriterIntegrationTests(unittest.TestCase):
    def test_plan_runs_before_native_rewriting_and_preserves_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);nso=root/'main.nso';out=root/'out';out.mkdir()
            data=bytearray(0x190);data[:4]=b'NSO0';data[0x40:0x50]=b'\x11'*16
            for i,segment in enumerate(((0x100,0,64),(0x140,0x1000,64),(0x180,0x2000,16))):
                struct.pack_into('<III',data,0x10+i*0x10,*segment)
            struct.pack_into('<III',data,0x60,64,64,16)
            data[0x100:0x140]=bytes.fromhex('1f2003d5')*16
            struct.pack_into('<I',data,0x104,0x1000)
            data[0x140:0x144]=b'MOD0';struct.pack_into('<i',data,0x150,32)
            data[0x160:0x164]=bytes((1,3,3,3))  # empty, valid eh_frame_hdr
            nso.write_bytes(data);before=hashlib.sha256(data).hexdigest()
            plan=dict(title_id='0100a0800b83a000',build_id='11'*16,module='main',nso_sha256=before,
                      patches=[dict(id='synthetic-svc',category='fps',enabled=True,
                                    writes=[dict(offset=0,expected='1f2003d5',replace='010000d4')])])
            path=root/'plan.json';path.write_text(json.dumps(plan))
            env=dict(os.environ,DM_TARGET='linux',DM_COMMUNITY_TITLE_ID=plan['title_id'],DM_COMMUNITY_PATCH_PLAN=str(path))
            tool=Path(__file__).resolve().parents[1]/'patch_nso.py'
            result=subprocess.run([sys.executable,str(tool),str(nso),str(out),'main'],env=env,text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stdout+'\n'+result.stderr)
            receipt=json.loads((out/'main.community.json').read_text())
            self.assertEqual(receipt['selected'],['synthetic-svc'])
            self.assertEqual(receipt['written_bytes'],4)
            patched=(out/'main.patched.bin').read_bytes()
            self.assertNotEqual(patched[:4],bytes.fromhex('010000d4'))
            self.assertEqual(receipt['rewritten_sha256'],hashlib.sha256(patched).hexdigest())
            self.assertEqual(hashlib.sha256(nso.read_bytes()).hexdigest(),before)


if __name__=='__main__':unittest.main()
