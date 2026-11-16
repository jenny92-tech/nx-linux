# INPUT: Synthetic native-v6 images, our assembly source and optional Unicorn.
# OUTPUT: Atomic patch guards, assembled-byte parity and ARM64 ABI/read-boundary checks.
# POS: Asset-free host regressions for the optional 90447 classic resource policy.
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
import classic_resource_gate as gate
try:
    import unicorn as uc
    from unicorn import arm64_const as arm
except ImportError:
    uc = None


def fixture(size=0x3078000, profile=gate):
    image = bytearray(size)
    control = size - 0x4000
    struct.pack_into('<8sIIQII', image, control-256,
                     getattr(profile,'NATIVE_MAGIC',b'D2RHD904'), 6, getattr(profile,'NATIVE_SITES',8), control-0x3FC0, control, 0)
    for at, raw in profile.CONTEXT:
        image[at:at+len(bytes.fromhex(raw))] = bytes.fromhex(raw)
    return image


class ResourceGateGuards(unittest.TestCase):
    def test_source_assembly_matches_embedded_template_without_relocations(self):
        clang = shutil.which('clang')
        if not clang:
            self.skipTest('clang with AArch64 target required for assembly parity')
        source = (HERE/'classic_resource_gate.S.in').read_text()
        for name, value in {'STUB': gate.TEMPLATE_BASE, 'CONTROL': gate.TEMPLATE_BASE+0x1100,
                            'RESUME': gate.FILE_SITE+4}.items():
            source = source.replace('@'+name+'@', str(value))
        with tempfile.TemporaryDirectory() as directory:
            assembly = Path(directory)/'gate.S'; obj = assembly.with_suffix('.o')
            assembly.write_text(source)
            subprocess.run([clang, '-target', 'aarch64-linux-gnu', '-c', str(assembly), '-o', str(obj)], check=True)
            data = obj.read_bytes()
        self.assertEqual(data[:6], b'\x7fELF\x02\x01')
        self.assertEqual(struct.unpack_from('<H', data, 18)[0], 183)
        offset = struct.unpack_from('<Q', data, 40)[0]
        stride, count, strings_index = struct.unpack_from('<HHH', data, 58)
        sections = [struct.unpack_from('<IIQQQQIIQQ', data, offset+i*stride) for i in range(count)]
        strings = sections[strings_index]
        names = data[strings[4]:strings[4]+strings[5]]
        text = None
        for section in sections:
            name = names[section[0]:].split(b'\0', 1)[0]
            self.assertFalse(section[1] in (4, 9) and section[5], 'unresolved relocation')
            if name == b'.text':
                text = data[section[4]:section[4]+section[5]]
        self.assertEqual(text, gate.TEMPLATE)

    def test_rejected_context_and_layout_leave_input_unchanged(self):
        image = fixture()
        control = len(image)-0x4000
        positions = [at+i for at, raw in gate.CONTEXT for i in (0, len(bytes.fromhex(raw))-1)]
        positions += [control-256, control-248, control-244, control-232,
                      control-0x1000, control+256, len(image)-1]
        for position in positions:
            image[position] ^= 1
            before = bytes(image)
            with self.subTest(offset=hex(position)), self.assertRaises(ValueError):
                gate.prepare_resource_gate(image)
            self.assertEqual(image, before)
            image[position] ^= 1
        for mode in (True, False, -1, 2, 4, 3.0, '3'):
            before = bytes(image)
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                gate.prepare_resource_gate(image, mode)
            self.assertEqual(image, before)
        for invalid in (bytearray(32), image[:-1], bytes(image)):
            with self.assertRaises(ValueError):
                gate.prepare_resource_gate(invalid)

    def test_relocation_footprint_and_double_preparation(self):
        for size in (0x3078000, 0x3298000):
            image = fixture(size); before = bytes(image)
            report = gate.prepare_resource_gate(image)
            ranges = [(gate.FILE_SITE, 4), (gate.PREVIEW_SITE, 4), (gate.DESCRIPTOR_SITE, 4), *((at, 4) for at in gate.WIDGET_SITES),
                      (report['stub'], len(gate.TEMPLATE)), (report['control'], 4)]
            cursor = 0
            for offset, length in sorted(ranges):
                self.assertEqual(image[cursor:offset], before[cursor:offset])
                cursor = offset+length
            self.assertEqual(image[cursor:], before[cursor:])
            self.assertEqual(report['control']-report['stub'], 0x1100)
            before = bytes(image)
            with self.assertRaises(ValueError):
                gate.prepare_resource_gate(image)
            self.assertEqual(image, before)


@unittest.skipIf(uc is None, 'instruction execution requires unicorn==2.1.4')
class ResourceGateInstructions(unittest.TestCase):
    PROFILE=gate
    IMAGE_SIZES=(0x3078000,0x3298000)
    def test_real_instructions_preserve_abi_and_reject_before_original_open(self):
        gate=self.PROFILE
        cases = [
            ('data/hd/character/new/never-seen.model', True), ('DATA\\HD\\ENV\\new.tile', True),
            ('data/hd/future_content/new.type', True), ('data/hd/globalish/not_ui.texture', True),
            ('data/hd/vfx/new.particles', True), ('data/hd/', True),
            ('data/global/chars/BA/BAHDlitNUHTH.dc6', False), ('data/hd/global/ui/panel.sprite', False),
            ('DATA\\HD\\GLOBAL\\fonts/new.ttf', True), ('data/local/lng/chinese.json', False),
            ('data/hd2/other', False), ('data/h', False), ('', False), (None, False),
            ('data/hd/future/'+('x'*400), True), ('data/hd/shaders/vs_default.bin', False),
            ('data/hd/local/lng/file.json', False), ('DATA/HD/TEXTURE_DESC_CACHE.JSON', False),
            ('data/hd/model_lod_desc.bin', False), ('data/hd/pak_manifest.json', False),
            ('data/hd/pak_manifest.json.extra', True), ('data/hd/global', True),
            ('data/hd/texture_desc_cache.jso', True), ('data/hd/ui/fonts/AnyFutureFont.ttf', False),
            ('data/hd/model_lod_desc.json', False), ('data/hd/env/weather/vis_event_map.json', False),
            ('data/hd/env/weatherish/new.json', True), ('DATA\\HD\\ENV\\LUT\\default.dds', False),
            ('data/hd/env/lut2/new.dds', True), ('data/hd/vfx/textures/default/white.texture', False),
            ('data/hd/vfx/textures/defaultish/new.texture', True), ('data/hd/items/items.json', False),
            ('DATA\\HD\\ITEMS\\SETS.JSON', False), ('data/hd/items/uniques.json', False),
            ('data/hd/items/items.json new.texture', True), ('data/hd/items/weapon/axe/hand_axe.json', True),
            ('data/hd/pak_manifest.json new', True),
            ('data/hd/global/act_1_preload_units.json', True),
            ('data/hd/global/act_9_preload_units.json', True),
            ('data/hd/global/assetlodmappingtable.json', True),
            ('data/hd/global/FootprintDecals.json', True),
            ('data/hd/global/future_world/new.type', True),
            ('data/hd/global/music/new.opus', False),
            ('data/hd/global/sfx/monster/new.opus', False),
            ('data/hd/global/video/act9/new.webm', False),
            ('data/hd/global/excel/controllersettings.json', False),
            ('data/hd/global/palette/transforms.json', False),
            ('data/hd/global/textures/black4x4_alb.texture', False),
        ]
        for prefix in gate.PRESERVED_PREFIXES:
            cases += [('data/hd/'+prefix+'new.asset', False),
                      ('data/hd/'+prefix[:-1]+'ish/new.asset', True)]
        for name in gate.PRESERVED_METADATA:
            cases += [('data/hd/'+name, False), ('data/hd/'+name+'.extra', True),
                      ('data/hd/'+name+' extra', True)]
        for size, base in zip(self.IMAGE_SIZES,(0x40000000,0x7130000000)):
            image = fixture(size,gate); report = gate.prepare_resource_gate(image)
            m = uc.Uc(uc.UC_ARCH_ARM64, uc.UC_MODE_ARM)
            for off, length in ((gate.FILE_SITE & ~4095, 4096), (report['stub'], 4096),
                                (report['control'] & ~4095, 0x4000)):
                m.mem_map(base+off, length)
                m.mem_write(base+off, bytes(image[off:off+length]))
                permission = uc.UC_PROT_READ | (uc.UC_PROT_WRITE if off == (report['control'] & ~4095) else uc.UC_PROT_EXEC)
                m.mem_protect(base+off, length, permission)
            pathpage, stack, sentinel = 0x90000000, 0x91000000, 0x92000000
            sp = stack+0x1000
            for address, length in ((pathpage, 4096), (stack, 8192), (sentinel, 4096)):
                m.mem_map(address, length)
            regs = [getattr(arm, 'UC_ARM64_REG_X%d' % i) for i in range(31)]
            stops = {base+gate.FILE_SITE+4, sentinel}
            def stop(machine, at, length, arg):
                if at in stops:
                    machine.emu_stop()
            m.hook_add(uc.UC_HOOK_CODE, stop)
            def setup(path, nzcv=0xA0000000):
                ptr = 0
                if path is not None:
                    raw = path.encode()+b'\0'
                    ptr = pathpage+4096-len(raw)  # NUL touches the last mapped byte.
                    m.mem_write(ptr, raw)
                before = [0x100000+i for i in range(31)]
                before[0], before[30] = ptr, sentinel
                for reg, value in zip(regs, before):
                    m.reg_write(reg, value)
                m.reg_write(arm.UC_ARM64_REG_SP, sp)
                m.reg_write(arm.UC_ARM64_REG_NZCV, nzcv)
                return before
            def reset(mode):
                m.mem_write(base+report['control'], bytes(0x3000))
                m.mem_write(base+report['control'], struct.pack('<I', mode))
            for mode in (0, 1, 3):
                reset(mode); totals = [0]*5
                for path, world in cases:
                    before = setup(path)
                    m.emu_start(base+gate.FILE_SITE, sentinel+4, count=10000)
                    denied = bool(mode == 3 and world)
                    expected = before[:]; expected[0] = 0 if denied else before[0]
                    with self.subTest(size=size, base=base, mode=mode, path=path):
                        self.assertEqual([m.reg_read(r) for r in regs], expected)
                        self.assertEqual(m.reg_read(arm.UC_ARM64_REG_PC), sentinel if denied else base+gate.FILE_SITE+4)
                        self.assertEqual(m.reg_read(arm.UC_ARM64_REG_SP), sp-(0 if denied else 32))
                        self.assertEqual(m.reg_read(arm.UC_ARM64_REG_NZCV), 0xA0000000)
                        if not denied:
                            self.assertEqual(bytes(m.mem_read(sp-32, 16)), struct.pack('<QQ', before[28], before[19]))
                        if mode and path is not None:
                            totals[0] += 1
                            normalized = path.lower().replace('\\', '/')
                            totals[3 if world else 2 if normalized.startswith('data/hd/') else 1] += 1
                            totals[4] += int(denied)
                        self.assertEqual(struct.unpack('<5I', m.mem_read(base+report['control']+4, 20)), tuple(totals))
                if mode:
                    row = bytes(m.mem_read(base+report['control']+256, 192))
                    self.assertEqual(struct.unpack_from('<Q', row)[0], sentinel)
                    self.assertEqual(row[16:].split(b'\0')[0], cases[0][0].encode())
            reset(3)
            for n in range(100):
                setup('data/hd/future/%d.texture' % n)
                m.emu_start(base+gate.FILE_SITE, sentinel+4, count=10000)
                self.assertEqual(m.reg_read(arm.UC_ARM64_REG_PC), sentinel)
            self.assertEqual(struct.unpack('<5I', m.mem_read(base+report['control']+4, 20)), (100, 0, 0, 100, 100))
            self.assertEqual(bytes(m.mem_read(base+report['control']+256+32*192, 192)), bytes(192))
            for off in sorted({gate.LOOKUP & ~4095} | {s & ~4095 for s in gate.WIDGET_SITES}):
                m.mem_map(base+off, 4096)
                m.mem_write(base+off, bytes(image[off:off+4096]))
                m.mem_protect(base+off, 4096, uc.UC_PROT_READ | uc.UC_PROT_EXEC)
            stops.add(base+gate.LOOKUP)
            stops.update(base+site+4 for site in gate.WIDGET_SITES)
            widget_cases = [(n, True) for n in ('Camera3DWidget', 'PlayerLoaderWidget', 'PlayerMoverWidget')]
            widget_cases += [(n, False) for n in ('ButtonWidget', 'ImageWidget', 'MainMenuHDPanel',
                'PlayerLoaderWidgetExtra', 'Player', '', None, 'camera3dwidget', 'PlayerMoverWidget2', 'Camera3D')]
            for mode in (0, 1, 3):
                reset(mode); seen = denied_count = 0
                for site in gate.WIDGET_SITES:
                    for name, targeted in widget_cases:
                        before = setup(name, 0x60000000)
                        m.emu_start(base+site, sentinel+4, count=10000)
                        denied = bool(mode == 3 and targeted)
                        expected = before[:]; expected[0] = 0 if denied else before[0]
                        expected[30] = base+site+4
                        self.assertEqual([m.reg_read(r) for r in regs], expected)
                        self.assertEqual(m.reg_read(arm.UC_ARM64_REG_PC), base+site+4 if denied else base+gate.LOOKUP)
                        self.assertEqual(m.reg_read(arm.UC_ARM64_REG_SP), sp)
                        self.assertEqual(m.reg_read(arm.UC_ARM64_REG_NZCV), 0x60000000)
                        seen += int(bool(mode and targeted)); denied_count += int(denied)
                        self.assertEqual(struct.unpack('<2I', m.mem_read(base+report['control']+24, 8)), (seen, denied_count))
            off = gate.PREVIEW_SITE & ~4095
            m.mem_map(base+off, 4096)
            m.mem_write(base+off, bytes(image[off:off+4096]))
            m.mem_protect(base+off, 4096, uc.UC_PROT_READ | uc.UC_PROT_EXEC)
            stops.update((base+gate.PREVIEW_SITE+4, base+gate.PREVIEW_CHILDREN))
            for mode in (0, 1, 3):
                reset(mode); seen = skipped = 0
                for state in (0, 1, 2, 3, 4, 0xFFFFFFFF):
                    before = setup(None, 0xB0000000)
                    before[0] = pathpage; m.reg_write(regs[0], pathpage)
                    m.mem_write(pathpage+0xE8C, struct.pack('<I', state))
                    m.emu_start(base+gate.PREVIEW_SITE, sentinel+4, count=10000)
                    expected = before[:]
                    if mode == 3:
                        expected[19] = pathpage  # Original child traversal's this pointer.
                    else:
                        expected[8] = state  # Displaced LDR W8, [X0, #0xe8c].
                    self.assertEqual([m.reg_read(r) for r in regs], expected)
                    self.assertEqual(m.reg_read(arm.UC_ARM64_REG_PC),
                                     base+(gate.PREVIEW_CHILDREN if mode == 3 else gate.PREVIEW_SITE+4))
                    self.assertEqual(m.reg_read(arm.UC_ARM64_REG_SP), sp)
                    self.assertEqual(m.reg_read(arm.UC_ARM64_REG_NZCV), 0xB0000000)
                    seen += int(bool(mode)); skipped += int(mode == 3)
                    self.assertEqual(struct.unpack('<2I', m.mem_read(base+report['control']+32, 8)), (seen, skipped))
            # Execute the actual original prologue, preserved child traversal and
            # epilogue. A visible 2D child still receives its virtual update once.
            stops.remove(base+gate.PREVIEW_CHILDREN)
            callback = 0x93000000
            m.mem_map(callback, 4096); m.mem_write(callback, bytes.fromhex('c0035fd6'))
            calls = []
            def observe_child(machine, at, length, arg):
                if at == callback:
                    calls.append(machine.reg_read(regs[0]))
            m.hook_add(uc.UC_HOOK_CODE, observe_child)
            m.mem_write(pathpage, bytes(4096))
            m.mem_write(pathpage+0x50, b'\1')
            m.mem_write(pathpage+0x58, struct.pack('<QQ', pathpage+0x100, 1))
            m.mem_write(pathpage+0x100, struct.pack('<Q', pathpage+0x200))
            m.mem_write(pathpage+0x200, struct.pack('<Q', pathpage+0x300))
            m.mem_write(pathpage+0x250, b'\1')
            m.mem_write(pathpage+0x318, struct.pack('<Q', callback))
            reset(3); before = setup(None)
            before[0] = pathpage; m.reg_write(regs[0], pathpage)
            m.emu_start(base+gate.PREVIEW_SITE-32, sentinel+4, count=10000)
            self.assertEqual(m.reg_read(arm.UC_ARM64_REG_PC), sentinel)
            self.assertEqual(m.reg_read(arm.UC_ARM64_REG_SP), sp)
            self.assertEqual([m.reg_read(r) for r in regs[19:]], before[19:])
            self.assertEqual(calls, [pathpage+0x200])
            self.assertEqual(struct.unpack('<2I', m.mem_read(base+report['control']+32, 8)), (1, 1))


@unittest.skipIf(uc is None, 'instruction execution requires unicorn==2.1.4')
class ResourceDescriptorInstructions(unittest.TestCase):
    PROFILE=gate
    IMAGE_SIZES=(0x3078000,0x3298000)
    def test_mixed_table_entries_reuse_policy_before_allocation_and_inline_decode(self):
        gate=self.PROFILE
        cases = [('data/hd/character/new.texture', True), ('data/hd/global/new.asset', True),
                 ('data/global/ui/panel', False), ('', False), (None, False),
                 ('DATA\\HD\\ENV\\new.texture', True)]
        cases += [('data/hd/'+p+'new.asset', False) for p in gate.PRESERVED_PREFIXES]
        cases += [('data/hd/'+p[:-1]+'ish/new.asset', True) for p in gate.PRESERVED_PREFIXES]
        cases += [('data/hd/'+p, False) for p in gate.PRESERVED_METADATA]
        cases += [('data/hd/'+p+'.extra', True) for p in gate.PRESERVED_METADATA]
        regs = [getattr(arm, 'UC_ARM64_REG_X%d' % i) for i in range(31)]
        for size, base in zip(self.IMAGE_SIZES,(0x40000000,0x7130000000)):
            source = fixture(size,gate); report = gate.prepare_resource_gate(source)
            machine = uc.Uc(uc.UC_ARCH_ARM64, uc.UC_MODE_ARM)
            for off, length in ((gate.DESCRIPTOR_SITE & ~4095, 4096), (report['stub'], 4096),
                                (report['control'] & ~4095, 0x4000)):
                machine.mem_map(base+off, length)
                machine.mem_write(base+off, bytes(source[off:off+length]))
                permission = uc.UC_PROT_WRITE if off == (report['control'] & ~4095) else uc.UC_PROT_EXEC
                machine.mem_protect(base+off, length, uc.UC_PROT_READ | permission)
            page, stack = 0x90000000, 0x91000000
            machine.mem_map(page, 4096); machine.mem_map(stack, 8192)
            stops = {base+n for n in (gate.DESCRIPTOR_SKIP, gate.DESCRIPTOR_ZERO, gate.DESCRIPTOR_NONZERO)}
            def stop(m, at, length, arg):
                if at in stops: m.emu_stop()
            machine.hook_add(uc.UC_HOOK_CODE, stop)
            for mode in (0, 1, 3):
                machine.mem_write(base+report['control'], bytes(0x3000))
                machine.mem_write(base+report['control'], struct.pack('<I', mode))
                seen = skipped = 0
                for name, world in cases:
                    for member_count in (0, 1, 24):
                        pointer = 0
                        if name is not None:
                            raw = name.encode()+b'\0'; pointer = page+4096-len(raw)
                            machine.mem_write(pointer, raw)
                        before = [0xF00000+i for i in range(31)]
                        before[10], before[21], before[30] = member_count, pointer, 0x6D726F66
                        for reg, value in zip(regs, before): machine.reg_write(reg, value)
                        machine.reg_write(arm.UC_ARM64_REG_SP, stack+4096)
                        machine.reg_write(arm.UC_ARM64_REG_NZCV, 0xB0000000)
                        machine.emu_start(base+gate.DESCRIPTOR_SITE, 0xFFFFFFFFFFFF, count=10000)
                        denied = mode == 3 and world
                        target = gate.DESCRIPTOR_SKIP if denied else (gate.DESCRIPTOR_NONZERO if member_count else gate.DESCRIPTOR_ZERO)
                        with self.subTest(size=size, mode=mode, name=name, members=member_count):
                            self.assertEqual(machine.reg_read(arm.UC_ARM64_REG_PC), base+target)
                            self.assertEqual([machine.reg_read(reg) for reg in regs], before)
                            self.assertEqual(machine.reg_read(arm.UC_ARM64_REG_SP), stack+4096)
                            self.assertEqual(machine.reg_read(arm.UC_ARM64_REG_NZCV), 0xB0000000)
                        seen += int(mode != 0); skipped += int(denied)
                        self.assertEqual(struct.unpack('<2I', machine.mem_read(base+report['control']+40, 8)), (seen, skipped))
                        self.assertEqual(bytes(machine.mem_read(base+report['control']+4, 36)), bytes(36))


if __name__ == '__main__':
    unittest.main()
