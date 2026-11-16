# INPUT: Synthetic prepared module layout and the shared instruction contract.
# OUTPUT: Every conditional path preserves arguments/SP and rejects changed sources.
# POS: Host semantic check of emitted ARM64 wrappers; no game assets or execution.
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(HERE))
from prepare_classic_native import (prepare, SUFFIXES, prepare_pool_sizes, POOL_CONFIG_OFFSET,
    POOL_CONFIG_WORDS, prepare_cpu_pool_size, CPU_POOL_CONFIG_OFFSET, CPU_POOL_CONFIG_WORDS,
    prepare_terrain_resolution, TERRAIN_64_PATCHES, TERRAIN_CONTEXT,
    RENDER_OBJECT_POOL_CONFIG_OFFSET, RENDER_OBJECT_POOL_CONFIG_WORDS,
    RENDER_OBJECT_POOL_GROWTH_OFFSET, RENDER_OBJECT_POOL_GROWTH_WORDS)
from install_game import verify_classic_modules, classic_shared_pool_minimum
from terrain_resolution import AUTO_CONTEXT
from classic_resource_gate import CONTEXT as RESOURCE_CONTEXT


def signed(v,bits):
    return v-(1<<bits) if v>>(bits-1) else v


def execute_stub(image,record,mask,blend,control,blend_at=0x2c0aca8):
    pc=record['stub'];resume=record['site']+4
    regs={i:0x100000+i for i in range(31)};before=dict(regs);sp=0x1000;stack={};calls=[]
    memory={control:mask,blend_at:blend}
    for _ in range(32):
        if pc==resume:
            if not calls:
                assert regs==before
            assert sp==0x1000 and not stack
            return calls
        w=struct.unpack_from('<I',image,pc)[0];next_pc=pc+4
        if w==0xa9bf47f0:
            sp-=16;stack[sp]=(regs[16],regs[17])
        elif w==0xa8c147f0:
            regs[16],regs[17]=stack.pop(sp);sp+=16
        elif w&0x9f000000==0x90000000:
            imm=(w>>29&3)|((w>>5&0x7ffff)<<2)
            regs[w&31]=(pc&~4095)+(signed(imm,21)<<12)
        elif w&0xffc00000==0xb9400000:
            regs[w&31]=memory[regs[w>>5&31]+(w>>10&4095)*4]
        elif w&0x7f000000==0x36000000:
            bit=(w>>19&31)|((w>>31)<<5)
            if not regs[w&31]&(1<<bit):next_pc=pc+(signed(w>>5&0x3fff,14)<<2)
        elif w&0x7f000000==0x35000000:
            if regs[w&31]&0xffffffff:next_pc=pc+(signed(w>>5&0x7ffff,19)<<2)
        elif w&0x7c000000==0x14000000:
            target=pc+(signed(w&0x3ffffff,26)<<2)
            if w>>31:
                assert target==record['target'] and sp==0x1000 and regs==before
                calls.append(target)
                # Model a legal original callee that clobbers scratch registers.
                regs[16]=0xdead;regs[17]=0xbeef
            else:next_pc=target
        else:
            raise AssertionError('unexpected instruction '+hex(w))
        pc=next_pc
    raise AssertionError('wrapper did not reach its reviewed resume')


class NativeHdTests(unittest.TestCase):
    def fixture(self,root):
        modules=root/'modules';modules.mkdir()
        policy=(HERE/'native_hd_policy.h').read_text()
        toggle=re.search(r'dm_native_hd_toggle\[\]=\{(.*?)\};',policy,re.S)[1]
        words=[int(s,16) for s in re.findall(r'0x[0-9a-f]+',toggle)]
        image=bytearray(0x3070040);struct.pack_into('<'+str(len(words))+'I',image,0x33e750,*words)
        struct.pack_into('<16I',image,POOL_CONFIG_OFFSET,*POOL_CONFIG_WORDS)
        struct.pack_into('<6I',image,CPU_POOL_CONFIG_OFFSET,*CPU_POOL_CONFIG_WORDS)
        struct.pack_into('<'+str(len(RENDER_OBJECT_POOL_CONFIG_WORDS))+'I',image,
                         RENDER_OBJECT_POOL_CONFIG_OFFSET,*RENDER_OBJECT_POOL_CONFIG_WORDS)
        struct.pack_into('<'+str(len(RENDER_OBJECT_POOL_GROWTH_WORDS))+'I',image,
                         RENDER_OBJECT_POOL_GROWTH_OFFSET,*RENDER_OBJECT_POOL_GROWTH_WORDS)
        for at, before, _ in TERRAIN_64_PATCHES:struct.pack_into('<I',image,at,before)
        for at, raw in TERRAIN_CONTEXT:image[at:at+len(bytes.fromhex(raw))]=bytes.fromhex(raw)
        for at, raw in AUTO_CONTEXT:image[at:at+len(bytes.fromhex(raw))]=bytes.fromhex(raw)
        for at, raw in RESOURCE_CONTEXT:image[at:at+len(bytes.fromhex(raw))]=bytes.fromhex(raw)
        sites=[(0x14f3198,[0x97efa5ce,0xa9447bfd,0x910143ff,0xd65f03c0]),
               (0x14f397c,[0x97efb5b0,0xa9477bfd,0xa9464ff4,0xa94557f6,0xa9445ff8,0x910203ff,0xd65f03c0]),
               (0x1527c8c,[0x97eee165,0xf9400268,0xf9400500,0xf8420c08,0xf9411108,0xd63f0100]),
               (0x10a7038,[0x94123828,0xf9401ffa,0x5280361b,0x360002b6,0xf9401be8,0xf9400100,0x97de4483]),
               (0x152f7a4,[0x94000f03,0x9000527e,0xd0005267,0xf9420fde,0xb0005266,0xf0005243,0xf940cce7]),
               (0x14ed8b8,[0x940004e6,0x1475e6ce,0xf9400fe3,0xa9421bfe,0xf94023e1,0xa94097e7,0xf9402be0]),
               (0x72765c,[0x94363c66,0x08dffee8,0x36003ea8,0xf0012a80,0x91308000,0x9406c638,0x08dfff48]),
               (0x727680,[0x94364ea7,0x08dfff48,0x36004108,0xf9455ac0,0x943651e4,0x08dffee8,0x360041c8])]
        struct.pack_into('<8I',image,0x15350d8,0xfc1a0fe8,0xa90167fa,0xa9025ff8,0xa90357f6,0xa9044ff4,0xa9057bfd,0x910143fd,0xf9401808)
        struct.pack_into('<8I',image,0x15333b0,0xd10743ff,0xa9195ffc,0xa91a57f6,0xa91b4ff4,0xa91c7bfd,0x910703fd,0xb0003549,0x3dc06d20)
        struct.pack_into('<8I',image,0x14eec50,0xd10743ff,0xf900cbfc,0xa91a57f6,0xa91b4ff4,0xa91c7bfd,0x910703fd,0xa940d033,0xaa0003f6)
        struct.pack_into('<8I',image,0x14b67f4,0xd10343ff,0xfd0033e8,0xa9076ffc,0xa90867fa,0xa9095ff8,0xa90a57f6,0xa90b4ff4,0xa90c7bfd)
        struct.pack_into('<8I',image,0x14bb11c,0xd10243ff,0xa90467fa,0xa9055ff8,0xa90657f6,0xa9074ff4,0xa9087bfd,0x910203fd,0xb000d468)
        struct.pack_into('<8I',image,0x14bbe20,0x6db63bef,0x6d0133ed,0x6d022beb,0x6d0323e9,0xa9046ffc,0xa90567fa,0xa9065ff8,0xa90757f6)
        for at,data in sites:struct.pack_into('<'+str(len(data))+'I',image,at,*data)
        for name in ('main','sdk','subsdk0'):
            for suffix in SUFFIXES:(modules/(name+'.'+suffix)).write_bytes(b'fixture')
        (modules/'main.patched.bin').write_bytes(image)
        (modules/'main.fixups.tsv').write_text('\n'.join('# '+k+'\t'+v for k,v in dict(
            BUILD_ID='c5e0c2db39a8d8a3ae2d5dfaaa68e16e',LINUX_TLS_OFFSET_IDX='8',KEY_PAGE_OFF='0x306c000',
            PATCH_OFF='0x3070000',TEXT_END='0x19e4b20',RO_OFF='0x19e5000',DATA_OFF='0x1ea4000').items())+'\n')
        return modules

    def test_mask_and_transition_paths_preserve_call_arguments_and_stack(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);source=self.fixture(root);before=hashlib.sha256((source/'main.patched.bin').read_bytes()).hexdigest()
            out=root/'result';report=prepare(source,out,'cc');image=(out/'main.patched.bin').read_bytes()
            for record in report['sites']:
                for mask in range(1<<len(report['sites'])):
                    for blend in (0,0x3f800000,0x3f000000,0x80000000,0x7fc00000):
                        calls=execute_stub(image,record,mask,blend,report['control_offset'])
                        self.assertEqual(len(calls),0 if mask&record['mask_bit'] and blend==0 else 1)
            self.assertEqual(image[0x306c000:0x3070000],(source/'main.patched.bin').read_bytes()[0x306c000:0x3070000])
            # C is required by the game: freezing it stopped frame progress.
            # Keep that complete call and the final HUD dispatcher unchanged.
            self.assertEqual(struct.unpack_from('<I',image,0x727690)[0],0x943651e4)
            self.assertNotIn(0x727690,[r['site'] for r in report['sites']])
            self.assertEqual(report['control_offset']%0x4000,0)
            self.assertEqual(len(image),report['control_offset']+0x4000)
            self.assertEqual(image[report['control_offset']:],bytes(0x4000))
            self.assertTrue(report['input_unchanged'])
            self.assertEqual(hashlib.sha256((source/'main.patched.bin').read_bytes()).hexdigest(),before)
            # Device installation must require the exact current format even
            # when an obsolete payload has a freshly matching transfer hash.
            self.assertTrue(verify_classic_modules(out)['runtime_validator_passed'])
            module=out/'main.patched.bin'
            with module.open('r+b') as stream:
                stream.seek(len(image)-0x4000-256+8)
                stream.write(struct.pack('<I',5))
            report['output_files']['main.patched.bin']['sha256']=hashlib.sha256(module.read_bytes()).hexdigest()
            (out/'classic-native.json').write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError,'format 6'):
                verify_classic_modules(out)
            module.write_bytes(image)
            with self.assertRaisesRegex(ValueError,'hash mismatch'):
                verify_classic_modules(out)
            with self.assertRaises(ValueError):prepare(source,out,'cc')

    def test_optional_smaller_pools_keep_native_format_and_source_unchanged(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);source=self.fixture(root)
            before=hashlib.sha256((source/'main.patched.bin').read_bytes()).hexdigest()
            out=root/'result';report=prepare(source,out,'cc',256,384)
            verified=verify_classic_modules(out)
            self.assertEqual(verified['graphics_pool_policy']['buffer_pool_mib'],256)
            self.assertEqual(verified['graphics_pool_policy']['texture_pool_mib'],384)
            self.assertFalse(report['graphics_pool_policy']['runtime_rendering_verified'])
            image=(out/'main.patched.bin').read_bytes()
            # Decode MOVZ operands independently of the preparation expression.
            for index,mib,reg in ((0,384,9),(10,256,8)):
                word=struct.unpack_from('<I',image,POOL_CONFIG_OFFSET+index*4)[0]
                self.assertEqual(word&31,reg)
                self.assertEqual(word&0xff800000,0xd2800000)
                self.assertEqual(((word>>5)&65535)<<(16*((word>>21)&3)),mib*1024*1024)
            self.assertEqual(hashlib.sha256((source/'main.patched.bin').read_bytes()).hexdigest(),before)

    def test_automatic_terrain_preserves_native_validator_and_installer(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);source=self.fixture(root);original=(source/'main.patched.bin').read_bytes()
            out=root/'automatic';report=prepare(source,out,'cc',256,320,terrain_auto=True)
            verified=verify_classic_modules(out)
            self.assertTrue(verified['terrain_quality_policy']['automatic'])
            self.assertTrue(report['runtime_validator_passed'])
            self.assertEqual(verified['graphics_pool_policy']['texture_pool_mib'],320)
            runtime=b'LINUX_SHARED_POOL_SKIP_COMPRESSIBLE\0LINUX_3D_SCALE_WARMUP'
            self.assertEqual(classic_shared_pool_minimum(verified,runtime),256)
            for missing in (b'',b'LINUX_SHARED_POOL_SKIP_COMPRESSIBLE',b'LINUX_3D_SCALE_WARMUP'):
                with self.assertRaisesRegex(ValueError,'C89 runtime'):
                    classic_shared_pool_minimum(verified,missing)
            self.assertEqual((source/'main.patched.bin').read_bytes(),original)
            image=(out/'main.patched.bin').read_bytes()
            self.assertEqual(image[0x306c000:0x3070000],original[0x306c000:0x3070000])
            self.assertEqual(image[report['control_offset']:],bytes(0x4000))

    def test_installer_pool_floor_preserves_defaults_and_rejects_invalid_capacities(self):
        self.assertEqual(classic_shared_pool_minimum({},b''),384)
        c87={'graphics_pool_policy':{'buffer_pool_mib':384,'texture_pool_mib':512}}
        self.assertEqual(classic_shared_pool_minimum(c87,b''),384)
        for invalid in (True,256.0,'256',63,401):
            report={'graphics_pool_policy':{'buffer_pool_mib':invalid,'texture_pool_mib':320}}
            with self.assertRaisesRegex(ValueError,'capacities'):
                classic_shared_pool_minimum(report,b'LINUX_SHARED_POOL_SKIP_COMPRESSIBLE')

    def test_resource_gate_combines_with_terrain_and_survives_distribution(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=self.fixture(root);out=root/'gated'
            original=(source/'main.patched.bin').read_bytes()
            report=prepare(source,out,'cc',256,320,terrain_auto=True,classic_resource_gate=True)
            verified=verify_classic_modules(out)
            gate=verified['resource_load_policy'];image=(out/'main.patched.bin').read_bytes()
            self.assertEqual(gate['mode'],3)
            self.assertTrue(gate['before_decode'])
            self.assertTrue(verified['terrain_quality_policy']['automatic'])
            self.assertEqual(gate['control'],report['control_offset']+256)
            self.assertEqual(struct.unpack_from('<I',image,gate['control'])[0],3)
            self.assertEqual(hashlib.sha256(image[gate['stub']:gate['stub']+gate['code_bytes']]).hexdigest(),gate['code_sha256'])
            self.assertEqual((source/'main.patched.bin').read_bytes(),original)
            self.assertEqual(image[0x306c000:0x3070000],original[0x306c000:0x3070000])
            self.assertIn('classic_resource_gate.S.in',report['source_sha256'])

    def test_combined_capacities_preserve_flags_and_installer_binding(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);source=self.fixture(root);out=root/'combined'
            original=(source/'main.patched.bin').read_bytes()
            report=prepare(source,out,'cc',384,512,128)
            verified=verify_classic_modules(out)
            image=(out/'main.patched.bin').read_bytes()
            for offset,mib,reg in ((POOL_CONFIG_OFFSET,512,9),
                    (POOL_CONFIG_OFFSET+40,384,8),(CPU_POOL_CONFIG_OFFSET+4,128,10)):
                word=struct.unpack_from('<I',image,offset)[0]
                self.assertEqual(word&31,reg)
                self.assertEqual(((word>>5)&65535)<<(16*((word>>21)&3)),mib*2**20)
            # The GPU flags live in the following MOVK instructions.
            self.assertEqual(image[POOL_CONFIG_OFFSET+4:POOL_CONFIG_OFFSET+40],
                             original[POOL_CONFIG_OFFSET+4:POOL_CONFIG_OFFSET+40])
            self.assertEqual(image[POOL_CONFIG_OFFSET+44:POOL_CONFIG_OFFSET+64],
                             original[POOL_CONFIG_OFFSET+44:POOL_CONFIG_OFFSET+64])
            self.assertEqual(verified['cpu_pool_policy']['cpu_pool_mib'],128)
            self.assertEqual(verified['graphics_pool_policy']['texture_pool_mib'],512)
            self.assertFalse(report['runtime_rendering_verified'])
            self.assertEqual((source/'main.patched.bin').read_bytes(),original)
            for name in ('sdk','subsdk0'):
                for suffix in SUFFIXES:
                    self.assertEqual((source/(name+'.'+suffix)).read_bytes(),
                                     (out/(name+'.'+suffix)).read_bytes())

    def test_optional_render_object_batch_is_bound_to_the_prepared_module(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);source=self.fixture(root);original=(source/'main.patched.bin').read_bytes()
            out=root/'small-batches';report=prepare(source,out,'cc',render_object_batch=64)
            verified=verify_classic_modules(out);image=(out/'main.patched.bin').read_bytes()
            self.assertEqual(verified['render_object_pool_policy']['units_per_chunk'],64)
            at=RENDER_OBJECT_POOL_CONFIG_OFFSET+9*4
            self.assertEqual(struct.unpack_from('<I',image,at)[0],0xf2c00809)
            lo=RENDER_OBJECT_POOL_GROWTH_OFFSET;hi=lo+4*len(RENDER_OBJECT_POOL_GROWTH_WORDS)
            self.assertEqual(image[lo:hi],original[lo:hi])
            self.assertFalse(report['render_object_pool_policy']['growth_code_changed'])
            self.assertFalse(report['render_object_pool_policy']['runtime_rendering_verified'])
            self.assertEqual((source/'main.patched.bin').read_bytes(),original)
            self.assertEqual(report['output_files']['main.patched.bin']['sha256'],hashlib.sha256(image).hexdigest())
            with self.assertRaises(ValueError):
                prepare(source,root/'invalid','cc',render_object_batch=128)
            self.assertFalse((root/'invalid/classic-native.json').exists())

    def test_changed_call_and_classic_toggle_are_rejected(self):
        for offset in (0x14f3198,0x33e750,0x15350d8,0x152f7a4,0x14ed8b8,0x15333b0,0x14eec50,
                       0x72765c,0x727680,0x727690,0x14b67f4,0x14bb11c):
            with tempfile.TemporaryDirectory() as d:
                root=Path(d);source=self.fixture(root);path=source/'main.patched.bin'
                with path.open('r+b') as f:
                    f.seek(offset);original=f.read(1);f.seek(offset);f.write(bytes([original[0]^255]))
                with self.assertRaises(subprocess.CalledProcessError):prepare(source,root/'result','cc')
                self.assertFalse((root/'result/classic-native.json').exists())


class GraphicsPoolTests(unittest.TestCase):
    def fixture(self):
        image=bytearray(POOL_CONFIG_OFFSET+128)
        struct.pack_into('<16I',image,POOL_CONFIG_OFFSET,*POOL_CONFIG_WORDS)
        image[-32:]=bytes(range(32))
        return image

    def test_only_capacity_operands_change_and_original_flags_survive(self):
        for buffer_mib,texture_mib in ((128,256),(256,384),(400,710),(64,64),(399,709)):
            image=self.fixture();before=bytes(image)
            report=prepare_pool_sizes(image,buffer_mib,texture_mib)
            for index,mib,reg in ((0,texture_mib,9),(10,buffer_mib,8)):
                offset=POOL_CONFIG_OFFSET+index*4
                word=struct.unpack_from('<I',image,offset)[0]
                self.assertEqual(word&31,reg)
                self.assertEqual(word&0xff800000,0xd2800000)
                self.assertEqual(((word>>5)&65535)<<(16*((word>>21)&3)),mib*1024*1024)
            allowed={POOL_CONFIG_OFFSET+i for j in (0,10) for i in range(j*4,j*4+4)}
            self.assertTrue(all(a==b or i in allowed for i,(a,b) in enumerate(zip(before,image))))
            self.assertEqual(report['buffer_pool_mib'],buffer_mib)
            self.assertEqual(report['texture_pool_mib'],texture_mib)
            self.assertFalse(report['graphics_quality_changed'])

    def test_changed_surrounding_instruction_is_rejected_without_partial_write(self):
        for index in range(16):
            image=self.fixture();image[POOL_CONFIG_OFFSET+index*4]^=1;before=bytes(image)
            with self.assertRaises(ValueError):prepare_pool_sizes(image,256,384)
            self.assertEqual(image,before)

    def test_invalid_sizes_and_short_input_are_atomic(self):
        for values in ((63,384),(401,384),(256,63),(256,711),(True,384),(256,False),(256.0,384),('256',384)):
            image=self.fixture();before=bytes(image)
            with self.assertRaises(ValueError):prepare_pool_sizes(image,*values)
            self.assertEqual(image,before)
        image=bytearray(32);before=bytes(image)
        with self.assertRaises(ValueError):prepare_pool_sizes(image,256,384)
        self.assertEqual(image,before)

    def test_unchanged_defaults_and_double_preparation(self):
        image=self.fixture();before=bytes(image)
        self.assertIsNone(prepare_pool_sizes(image))
        self.assertEqual(image,before)
        report=prepare_pool_sizes(image,128,None)
        self.assertEqual(report['texture_pool_mib'],710)
        before=bytes(image)
        with self.assertRaises(ValueError):prepare_pool_sizes(image,128,256)
        self.assertEqual(image,before)


class CpuPoolTests(unittest.TestCase):
    def fixture(self):
        image=bytearray(CPU_POOL_CONFIG_OFFSET+128)
        struct.pack_into('<6I',image,CPU_POOL_CONFIG_OFFSET,*CPU_POOL_CONFIG_WORDS)
        return image

    def test_capacity_instruction_and_surrounding_configuration(self):
        for mib in (64,96,128,192,250):
            image=self.fixture();before=bytes(image);report=prepare_cpu_pool_size(image,mib)
            word=struct.unpack_from('<I',image,CPU_POOL_CONFIG_OFFSET+4)[0]
            self.assertEqual(word&31,10)
            self.assertEqual(word&0xffe00000,0x52a00000)
            self.assertEqual(((word>>5)&65535)<<16,mib*1024*1024)
            self.assertEqual(image[:CPU_POOL_CONFIG_OFFSET+4],before[:CPU_POOL_CONFIG_OFFSET+4])
            self.assertEqual(image[CPU_POOL_CONFIG_OFFSET+8:],before[CPU_POOL_CONFIG_OFFSET+8:])
            self.assertFalse(report['runtime_rendering_verified'])
            self.assertFalse(report['physical_memory_saving_verified'])

    def test_rejects_changed_context_and_invalid_capacity_without_writing(self):
        for index in range(6):
            image=self.fixture();image[CPU_POOL_CONFIG_OFFSET+index*4]^=1;before=bytes(image)
            with self.assertRaises(ValueError):prepare_cpu_pool_size(image,128)
            self.assertEqual(image,before)
        for mib in (63,251,True,128.0,'128'):
            image=self.fixture();before=bytes(image)
            with self.assertRaises(ValueError):prepare_cpu_pool_size(image,mib)
            self.assertEqual(image,before)
        image=bytearray(32);before=bytes(image)
        with self.assertRaises(ValueError):prepare_cpu_pool_size(image,128)
        self.assertEqual(image,before)

    def test_default_preserves_original_and_repeated_patch_is_rejected(self):
        image=self.fixture();before=bytes(image)
        self.assertIsNone(prepare_cpu_pool_size(image));self.assertEqual(image,before)
        prepare_cpu_pool_size(image,128);before=bytes(image)
        with self.assertRaises(ValueError):prepare_cpu_pool_size(image,128)
        self.assertEqual(image,before)

    def test_prepared_receipt_is_accepted_by_the_normal_installer(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=NativeHdTests().fixture(root);out=root/'cpu128'
            before=hashlib.sha256((source/'main.patched.bin').read_bytes()).hexdigest()
            report=prepare(source,out,'cc',cpu_pool_mib=128)
            verified=verify_classic_modules(out)
            self.assertEqual(verified['cpu_pool_policy']['cpu_pool_mib'],128)
            self.assertTrue(report['input_unchanged'])
            self.assertEqual(hashlib.sha256((source/'main.patched.bin').read_bytes()).hexdigest(),before)


class TerrainResolutionTests(unittest.TestCase):
    def fixture(self):
        image=bytearray(0x1bc0000)
        for at,before,_ in TERRAIN_64_PATCHES:struct.pack_into('<I',image,at,before)
        for at,raw in TERRAIN_CONTEXT:image[at:at+len(bytes.fromhex(raw))]=bytes.fromhex(raw)
        return image

    def test_size_query_dispatch_and_recreation_remain_consistent(self):
        image=self.fixture();before=bytes(image);report=prepare_terrain_resolution(image,64)
        word=lambda at:struct.unpack_from('<I',image,at)[0]
        def movz(at,register):
            value=word(at)
            self.assertEqual(value&0xffe0001f,0x52800000|register)
            return value>>5&0xffff
        for atlas,tile,dispatch,register in ((0x153a920,0x153a92c,0x153a938,9),
                                              (0x15411a4,0x15411b0,0x15411bc,8)):
            self.assertEqual(movz(atlas,register),18*movz(tile,9))
            self.assertEqual(movz(tile,9),8*movz(dispatch,9))
            self.assertEqual(movz(atlas,register),1152)
        compare=word(0x1541168)
        self.assertEqual(compare&0xffc003ff,0x7100011f)  # cmp w8, immediate
        self.assertEqual(compare>>10&0xfff,1152)
        low,high=word(0x15431fc),word(0x1543200)
        self.assertEqual(low&0xffe0001f,0xd2800000)
        self.assertEqual(high&0xffe0001f,0xf2c00000)
        query=((low>>5)&0xffff)|(((high>>5)&0xffff)<<32)
        self.assertEqual(query,1152|(1152<<32))
        end=0
        for at,_,_ in sorted(TERRAIN_64_PATCHES):
            self.assertEqual(image[end:at],before[end:at]);end=at+4
        self.assertEqual(image[end:],before[end:])
        self.assertEqual(report['tile_grid'],[18,18])
        self.assertFalse(report['pool_capacity_changed'])

    def test_every_changed_guard_and_invalid_input_reject_before_any_write(self):
        for at in [p[0] for p in TERRAIN_64_PATCHES]+[p[0] for p in TERRAIN_CONTEXT]:
            image=self.fixture();image[at]^=1;before=bytes(image)
            with self.assertRaises(ValueError):prepare_terrain_resolution(image,64)
            self.assertEqual(image,before)
        for side in (0,32,63,128,True,64.0,'64'):
            image=self.fixture();before=bytes(image)
            with self.assertRaises(ValueError):prepare_terrain_resolution(image,side)
            self.assertEqual(image,before)
        with self.assertRaises(ValueError):prepare_terrain_resolution(bytearray(32),64)
        image=self.fixture();before=bytes(image)
        self.assertIsNone(prepare_terrain_resolution(image));self.assertEqual(image,before)
        prepare_terrain_resolution(image,64);before=bytes(image)
        with self.assertRaises(ValueError):prepare_terrain_resolution(image,64)
        self.assertEqual(image,before)

    def test_terrain_and_pool_policy_share_one_verified_module_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=NativeHdTests().fixture(root);out=root/'terrain64'
            before=hashlib.sha256((source/'main.patched.bin').read_bytes()).hexdigest()
            report=prepare(source,out,'cc',buffer_pool_mib=256,texture_pool_mib=320,terrain_tile_side=64)
            verified=verify_classic_modules(out)
            self.assertEqual(verified['terrain_quality_policy']['atlas_side'],1152)
            self.assertEqual(verified['graphics_pool_policy']['texture_pool_mib'],320)
            self.assertTrue(report['input_unchanged'])
            self.assertEqual(hashlib.sha256((source/'main.patched.bin').read_bytes()).hexdigest(),before)


if __name__=='__main__':
    unittest.main()
