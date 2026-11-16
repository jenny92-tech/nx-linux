# INPUT: Synthetic guarded module and optional Unicorn ARM64 instruction engine.
# OUTPUT: Real emitted instructions preserve registers and resize/create coherently.
# POS: Host-only semantic regression; no game assets or device are required.
import struct
import unittest
import sys
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))

from prepare_classic_native import prepare_terrain_resolution, TERRAIN_64_PATCHES, TERRAIN_CONTEXT
from terrain_resolution import AUTO_CONTEXT, tile_for_height

try:
    import unicorn as uc
    from unicorn import arm64_const as arm
except ImportError:
    uc = None


def fixture():
    image = bytearray(0x3298000)
    for at, before, _ in TERRAIN_64_PATCHES:
        struct.pack_into('<I', image, at, before)
    for at, raw in (*TERRAIN_CONTEXT, *AUTO_CONTEXT):
        image[at:at+len(bytes.fromhex(raw))] = bytes.fromhex(raw)
    header = len(image)-0x4000-256
    image[header:header+8] = b'D2RHD904'
    struct.pack_into('<IIQI', image, header+8, 6, 8, 0x3292318, len(image)-0x4000)
    return image


class TerrainPolicyTests(unittest.TestCase):
    def test_height_policy_quantization_and_limits(self):
        for height, tile in ((360,32),(480,48),(540,48),(720,64),(1080,96),(1440,128),(2160,128)):
            self.assertEqual(tile_for_height(height),tile)
        for h in range(1,2161):
            t=tile_for_height(h)
            self.assertTrue(32<=t<=128 and t%8==0)
            if h>1:self.assertGreaterEqual(t,tile_for_height(h-1))
        for h in (0,-1,2161,720.,True):
            with self.assertRaises(ValueError):tile_for_height(h)

    def test_guards_and_padding_fail_before_any_mutation(self):
        original=fixture()
        for at in [r[0] for r in AUTO_CONTEXT]+[0x32924A0,len(original)-0x4000-256+8]:
            image=bytearray(original);image[at]^=1;before=bytes(image)
            with self.assertRaises(ValueError):prepare_terrain_resolution(image,automatic=True)
            self.assertEqual(image,before)
        image=bytearray(original);prepare_terrain_resolution(image,automatic=True);before=bytes(image)
        with self.assertRaises(ValueError):prepare_terrain_resolution(image,automatic=True)
        self.assertEqual(image,before)
        with self.assertRaises(ValueError):prepare_terrain_resolution(bytearray(original),64,True)

    @unittest.skipUnless(uc,'install Unicorn to execute ARM64 terrain semantics')
    def test_actual_instructions_init_rebuild_query_and_registers(self):
        image=fixture();report=prepare_terrain_resolution(image,automatic=True)
        base=0x40000000;renderer=0x70000000;stack=0x80000000;sp=stack+0x1000
        machine=uc.Uc(uc.UC_ARCH_ARM64,uc.UC_MODE_ARM)
        machine.mem_map(base,len(image));machine.mem_write(base,bytes(image))
        machine.mem_map(renderer,0x10000);machine.mem_map(stack,0x2000)
        regs=[getattr(arm,'UC_ARM64_REG_X%d'%i) for i in range(31)]
        def seed():
            values=[0x100000+i for i in range(31)]
            for reg,value in zip(regs,values):machine.reg_write(reg,value)
            machine.reg_write(arm.UC_ARM64_REG_SP,sp);machine.reg_write(arm.UC_ARM64_REG_NZCV,0xA0000000)
            return values
        def run(start,end):
            machine.emu_start(base+start,base+end,count=200)
            self.assertEqual(machine.reg_read(arm.UC_ARM64_REG_PC),base+end)
            self.assertEqual(machine.reg_read(arm.UC_ARM64_REG_SP),sp)
        def values():return [machine.reg_read(r) for r in regs]
        def parameters():return struct.unpack('<3I',machine.mem_read(base+0x30298F0,12))
        def memory(at,fmt,*items):machine.mem_write(at,struct.pack(fmt,*items))
        for height in (1,360,480,540,720,721,1080,1440,2160,0xffffffff):
            expected=max(32,min(128,((height+89)//90)*8))
            before=seed();memory(base+0x301EAB0,'<Q',renderer);memory(renderer+0xEE7C,'<I',height)
            run(0x153A91C,0x153A940)
            self.assertEqual(parameters(),(18*expected,expected//8,expected))
            self.assertEqual([v for i,v in enumerate(values()) if i not in (8,9)],
                             [v for i,v in enumerate(before) if i not in (8,9)])
            self.assertEqual(machine.reg_read(arm.UC_ARM64_REG_NZCV),0xA0000000)
            before=seed();run(0x15431FC,0x1543208)
            self.assertEqual(values()[0],(18*expected)*0x100000001)
            self.assertEqual(values()[1],1)
            self.assertEqual(values()[2:],before[2:])
            for old in (18*expected,2305):
                before=seed();memory(base+0x30298F0,'<3I',old,77,88)
                machine.reg_write(regs[2],height);machine.reg_write(regs[21],base+0x3029000)
                run(0x1541168,0x154116C)
                self.assertEqual(parameters(),(old,77,88))  # no early publication
                self.assertEqual(bool(machine.reg_read(arm.UC_ARM64_REG_NZCV)&0x40000000),old==18*expected)
                self.assertEqual(struct.unpack('<I',machine.mem_read(sp+8,4))[0],expected)
                # Model the intervening original callees clobbering volatile state.
                for i in range(18):machine.reg_write(regs[i],0xdead000+i)
                run(0x15411A4,0x15411C8)
                self.assertEqual(parameters(),(18*expected,expected//8,expected))
                self.assertEqual(values()[0],before[19])
        for ptr,height in ((0,720),(renderer,0)):
            seed();memory(base+0x301EAB0,'<Q',ptr);memory(renderer+0xEE7C,'<I',height)
            run(0x153A91C,0x153A940);self.assertEqual(parameters(),(1152,8,64))
        seed();memory(base+0x30298F0,'<I',0);run(0x15431FC,0x1543208)
        self.assertEqual(values()[0],1152*0x100000001)


if __name__=='__main__':unittest.main()
