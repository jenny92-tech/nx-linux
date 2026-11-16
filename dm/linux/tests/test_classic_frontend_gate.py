# INPUT: Synthetic native-v6 images and the reviewed root callback instruction template.
# OUTPUT: Atomic guards, assembler parity and ARM64 lease/ABI regression checks.
# POS: Asset-free tests; no game, device or original files required.
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
import classic_frontend_gate as frontend
import classic_resource_gate as gate
import classic_resource_94012 as gate94012
try:
    import unicorn as uc
    from unicorn import arm64_const as arm
except ImportError:
    uc = None


def fixture(size, game_build='90447'):
    profile = gate94012 if game_build == '94012' else gate
    callbacks = frontend.CALLBACKS_94012 if game_build == '94012' else frontend.CALLBACKS
    image = bytearray(size)
    control = size - 0x4000
    struct.pack_into('<8sIIQII', image, control-256,
                     b'D2RHD940' if game_build == '94012' else b'D2RHD904', 6, 3 if game_build == '94012' else 8, control-0x3fc0, control, 0)
    for at, raw in profile.CONTEXT:
        image[at:at+len(bytes.fromhex(raw))] = bytes.fromhex(raw)
    for _, _, at, raw in callbacks:
        image[at:at+len(bytes.fromhex(raw))] = bytes.fromhex(raw)
    return image


class FrontendGateTests(unittest.TestCase):
    GAME_BUILD = '90447'
    IMAGE_SIZES = (0x3078000, 0x3298000)
    CALLBACKS = frontend.CALLBACKS
    PROFILE = gate
    def test_layout_guards_are_atomic_and_original_widget_lookup_is_retained(self):
        for size in self.IMAGE_SIZES:
            for _, _, site, _ in self.CALLBACKS:
                image = fixture(size,self.GAME_BUILD)
                image[site+28] ^= 1
                before = bytes(image)
                with self.assertRaises(ValueError):
                    gate.prepare_resource_gate(image, frontend=True, game_build=self.GAME_BUILD)
                self.assertEqual(image, before)
            image = fixture(size,self.GAME_BUILD)
            before = bytes(image)
            report = gate.prepare_resource_gate(image, frontend=True, game_build=self.GAME_BUILD)
            self.assertEqual(report['mode'], 1)
            self.assertFalse(report['flat_menu_required'])
            self.assertFalse(report['widget_before_constructor'])
            self.assertEqual(len(report['frontend_policy']['callbacks']), 6)
            for site in self.PROFILE.WIDGET_SITES:
                self.assertEqual(image[site:site+4], before[site:site+4])
            self.assertTrue(report['frontend_policy']['original_model_release_preserved'])
            self.assertFalse(report['frontend_policy']['forced_resource_free'])

    def test_assembly_matches_embedded_template(self):
        clang = shutil.which('clang')
        if not clang:
            self.skipTest('clang with AArch64 assembler required')
        code = bytearray()
        for index, (_, event, site, context) in enumerate(self.CALLBACKS):
            while len(code) % 16:
                code.append(0)
            stub = gate.TEMPLATE_BASE + frontend.OFFSET + len(code)
            bit = 1 << (index // 2)
            opened = event == 'open'
            values = dict(STUB=stub, CONTROL=gate.TEMPLATE_BASE+0x1100,
                RESUME=site+4, ORIGINAL=int.from_bytes(bytes.fromhex(context)[:4], 'little'),
                COUNTER=48 if opened else 52,
                LEASE_OPERATION='orr w11, w11, #%d' % bit if opened else 'and w11, w11, #0x%x' % (0xffffffff ^ bit))
            source = (HERE/'classic_frontend_gate.S.in').read_text()
            for key, value in values.items():
                source = source.replace('@'+key+'@', str(value))
            with tempfile.TemporaryDirectory() as temporary:
                assembly = Path(temporary)/'gate.S'
                obj = assembly.with_suffix('.o')
                assembly.write_text(source)
                subprocess.run([clang, '-target', 'aarch64-linux-gnu', '-c', str(assembly), '-o', str(obj)], check=True)
                elf = obj.read_bytes()
            offset = struct.unpack_from('<Q', elf, 40)[0]
            stride, count, strings_index = struct.unpack_from('<HHH', elf, 58)
            sections = [struct.unpack_from('<IIQQQQIIQQ', elf, offset+i*stride) for i in range(count)]
            strings = sections[strings_index]
            names = elf[strings[4]:strings[4]+strings[5]]
            for section in sections:
                self.assertFalse(section[1] in (4, 9) and section[5])
                if names[section[0]:].split(b'\0', 1)[0] == b'.text':
                    code.extend(elf[section[4]:section[4]+section[5]])
        if self.GAME_BUILD == '90447':
            self.assertEqual(code, frontend.TEMPLATE)
        else:
            patches,_ = frontend.frontend_gate_patches(fixture(self.IMAGE_SIZES[0],self.GAME_BUILD),
                gate.TEMPLATE_BASE,gate.TEMPLATE_BASE+0x1100,gate.branch,self.GAME_BUILD)
            self.assertEqual(code,patches[-1][1])

    @unittest.skipIf(uc is None, 'Unicorn required for ARM64 instruction execution')
    def test_all_leases_relocate_and_preserve_registers_flags_and_original_stack(self):
        stack = 0x90000000
        for size, base in zip(self.IMAGE_SIZES,(0x7100000000,0x7200000000)):
            image = fixture(size,self.GAME_BUILD)
            report = gate.prepare_resource_gate(image, frontend=True, game_build=self.GAME_BUILD)
            for index, (_, event, site, context) in enumerate(self.CALLBACKS):
                for initial in range(8):
                    machine = uc.Uc(uc.UC_ARCH_ARM64, uc.UC_MODE_ARM)
                    machine.mem_map(base+report['stub'], 0x5000)
                    machine.mem_write(base+report['stub'], bytes(image[report['stub']:report['stub']+0x5000]))
                    machine.mem_map(base+(site & ~4095), 4096)
                    machine.mem_map(stack, 8192)
                    machine.mem_write(base+report['control']+64, struct.pack('<I', initial))
                    registers = {}
                    for number in range(31):
                        register = getattr(arm, 'UC_ARM64_REG_X'+str(number))
                        registers[register] = 0xabc00000+number
                        machine.reg_write(register, registers[register])
                    sp = stack+4096
                    machine.reg_write(arm.UC_ARM64_REG_SP, sp)
                    machine.reg_write(arm.UC_ARM64_REG_NZCV, 0xa0000000)
                    stop = base+site+4
                    machine.hook_add(uc.UC_HOOK_CODE, lambda m, at, length, target: m.emu_stop() if at == target else None, stop)
                    machine.emu_start(base+report['stub']+frontend.OFFSET+index*frontend.STRIDE, stop+4, count=1000)
                    self.assertEqual(machine.reg_read(arm.UC_ARM64_REG_PC), stop)
                    self.assertEqual({reg: machine.reg_read(reg) for reg in registers}, registers)
                    self.assertEqual(machine.reg_read(arm.UC_ARM64_REG_NZCV), 0xa0000000)
                    word = int.from_bytes(bytes.fromhex(context)[:4], 'little')
                    delta = {0xd101c3ff:112, 0xf81d0ff5:48, 0xd10143ff:80, 0xd10203ff:128, 0xd10183ff:96, 0xd10283ff:160, 0xd102c3ff:176}[word]
                    self.assertEqual(machine.reg_read(arm.UC_ARM64_REG_SP), sp-delta)
                    bit = 1 << (index // 2)
                    expected = initial | bit if event == 'open' else initial & ~bit
                    self.assertEqual(int.from_bytes(machine.mem_read(base+report['control']+64, 4), 'little'), expected)
                    self.assertEqual(int.from_bytes(machine.mem_read(base+report['control'], 4), 'little'), 1 if expected else 3)


class FrontendGate94012Tests(FrontendGateTests):
    GAME_BUILD = '94012'
    IMAGE_SIZES = (0x345c000,0x3460000)
    CALLBACKS = frontend.CALLBACKS_94012
    PROFILE = gate94012

    @unittest.skipIf(uc is None, 'Unicorn required for ARM64 instruction execution')
    def test_frontend_camera_metadata_survives_closed_root_without_opening_world(self):
        image = fixture(self.IMAGE_SIZES[0], self.GAME_BUILD)
        report = gate.prepare_resource_gate(image, frontend=True, game_build=self.GAME_BUILD)
        base, path, stop = 0x7300000000, 0x80000000, 0x90000000
        machine = uc.Uc(uc.UC_ARCH_ARM64, uc.UC_MODE_ARM)
        machine.mem_map(base+report['stub'], 4096)
        machine.mem_write(base+report['stub'], bytes(image[report['stub']:report['stub']+4096]))
        machine.mem_map(path, 4096)
        machine.mem_map(stop, 4096)
        cases = {
            'data/hd/env/vis/ui_frontend_charactercreate.json': 1,
            'DATA\\HD\\ENV\\VIS\\UI_FRONTEND_CHARACTERCREATE.JSON': 1,
            'data/hd/env/vis/ui_frontend_charactercreate.json.extra': 2,
            'data/hd/env/vis/ui_frontend_charactercreate.json/other': 2,
            'data/hd/env/vis/act1_outdoors.json': 2,
            'data/hd/character/player/barbarian.json': 2,
            'data/hd/global/act_1_preload_units.json': 2,
            'data/hd/global/ui/panel.sprite': 1,
        }
        for act in range(1, 6):
            name = 'data/hd/env/vis/ui_frontend_act%d_vignette.json' % act
            cases[name] = 1
            cases[name.upper().replace('/', '\\')] = 1
            cases[name+'.extra'] = 2
            cases[name+'/other'] = 2
        cases['data/hd/env/vis/ui_frontend_act6_vignette.json'] = 2
        for name, expected in cases.items():
            machine.mem_write(path, name.encode()+b'\0')
            machine.reg_write(arm.UC_ARM64_REG_X0, path)
            machine.reg_write(arm.UC_ARM64_REG_X30, stop)
            machine.emu_start(base+report['classifier'], stop, count=10000)
            self.assertEqual(machine.reg_read(arm.UC_ARM64_REG_X17), expected, name)


if __name__ == '__main__':
    unittest.main()
