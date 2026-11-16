# INPUT: Synthetic 94012 native trailers and build-specific resource guards.
# OUTPUT: Version mixing, patch atomicity and retained gameplay/display boundaries checked.
# POS: Asset-free host regression for the new classic-only profile.
from pathlib import Path
import copy
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import classic_resource_gate as gate
import classic_resource_94012 as profile
from classic_bundle import descriptor
from device_profiles import load_device, verify_components


def fixture():
    image=bytearray(0x345c000);control=len(image)-0x4000
    struct.pack_into('<8sIIQII',image,control-256,b'D2RHD940',6,3,control-0x3fc0,control,0)
    for at,raw in profile.CONTEXT:
        image[at:at+len(bytes.fromhex(raw))]=bytes.fromhex(raw)
    return image


class Profile94012Tests(unittest.TestCase):
    def test_resource_gates_reject_wrong_build_and_changed_new_context(self):
        image=fixture();before=bytes(image)
        with self.assertRaises(ValueError):gate.prepare_resource_gate(image)
        self.assertEqual(image,before)
        for at,_ in profile.CONTEXT:
            image[at]^=1;before=bytes(image)
            with self.assertRaises(ValueError):gate.prepare_resource_gate(image,game_build='94012')
            self.assertEqual(image,before);image[at]^=1

    def test_new_gate_keeps_extra_descriptor_fields_and_native_format(self):
        image=fixture();before=bytes(image)
        report=gate.prepare_resource_gate(image,game_build='94012')
        self.assertEqual(report['policy'],'classic-resource-gate-94012-v1')
        self.assertEqual(report['descriptor_zero'],0x16b91e0)
        self.assertEqual(report['descriptor_nonzero'],0x16b92f8)
        self.assertEqual(image[0x16b91e0:0x16b92e4],before[0x16b91e0:0x16b92e4])
        self.assertEqual(report['control']-report['stub'],0x1100)
        control=len(image)-0x4000
        self.assertEqual(struct.unpack_from('<8sII',image,control-256),(b'D2RHD940',6,3))

    def test_flat_profile_and_game_build_are_bound_together(self):
        device=load_device('rk3566-2gb')
        self.assertEqual(descriptor(device,'94012')['game_build'],'94012')
        self.assertIsNone(descriptor(device,'94012')['output_extent'])
        self.assertEqual(descriptor(device,'94012')['output_policy'],'sdl-drawable-aspect-v1')
        self.assertEqual(descriptor(device)['output_extent'],[960,720])
        with self.assertRaises(ValueError):descriptor(load_device('miniloong-pocket-one-rk3566-4gb'),'94012')
        native=dict(game_build='94012',graphics_pool_policy=dict(buffer_pool_mib=256,texture_pool_mib=320),
                    cpu_pool_policy=dict(cpu_pool_mib=250),resource_load_policy=dict(policy=profile.POLICY))
        frontend=dict(game_build='94012',policy='classic-frontend-no-preview-v3',handheld_character_sheet=True)
        verify_components(device,native,frontend)
        with self.assertRaises(ValueError):verify_components(device,native,dict(frontend,handheld_character_sheet=False))
        for changed in ('90447','94013'):
            damaged=copy.deepcopy(frontend);damaged['game_build']=changed
            with self.assertRaises(ValueError):verify_components(device,native,damaged)


# Run the shared instruction/ABI cases at both ASLR bases with the new sites.
import functools
import types
import test_classic_resource_gate as instruction_tests
_PROFILE=types.SimpleNamespace(**dict(vars(gate),**vars(profile)))
_PROFILE.NATIVE_MAGIC=b'D2RHD940'
_PROFILE.NATIVE_SITES=3
_PROFILE.prepare_resource_gate=functools.partial(gate.prepare_resource_gate,game_build='94012')

class ResourceGate94012Instructions(instruction_tests.ResourceGateInstructions):
    PROFILE=_PROFILE
    IMAGE_SIZES=(0x345c000,0x3460000)

class ResourceDescriptor94012Instructions(instruction_tests.ResourceDescriptorInstructions):
    PROFILE=_PROFILE
    IMAGE_SIZES=(0x345c000,0x3460000)


class Native94012Instructions(unittest.TestCase):
    def test_three_submission_wrappers_preserve_arguments_and_original_mode(self):
        import re
        import tempfile
        from prepare_classic_native import prepare, SUFFIXES, POOL_CONFIG_WORDS, CPU_POOL_CONFIG_WORDS
        from install_game import verify_classic_modules
        from test_native_hd_prepare import execute_stub
        here=Path(__file__).resolve().parents[1]
        header=(here/'native_hd_94012.h').read_text()
        image=bytearray(0x3220040)
        sites=re.findall(r'\{(0x[0-9a-f]+),(0x[0-9a-f]+),(0x[0-9a-f]+),\{([^}]+)\},([0-9]+)\}',header)
        targets=re.search(r'dm_native_hd_target_entry\[3\]\[8\]=\{(.*?)\};',header,re.S)[1]
        entries=re.findall(r'\{([^}]+)\}',targets)
        for (at,target,word,post,count),entry in zip(sites,entries):
            words=[int(word,16)]+[int(v,16) for v in post.split(',')]
            struct.pack_into('<'+str(len(words))+'I',image,int(at,16),*words)
            words=[int(v,16) for v in entry.split(',')]
            struct.pack_into('<8I',image,int(target,16),*words)
        toggle=re.search(r'dm_native_hd_toggle\[\]=\{(.*?)\};',header,re.S)[1]
        words=[int(v,16) for v in re.findall(r'0x[0-9a-f]+',toggle)]
        struct.pack_into('<'+str(len(words))+'I',image,0xb15d60,*words)
        struct.pack_into('<I',image,0xb15de8,0x54000228)
        struct.pack_into('<16I',image,0x8524a8,*POOL_CONFIG_WORDS)
        struct.pack_into('<6I',image,0x1348c8,*CPU_POOL_CONFIG_WORDS)
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);modules=root/'source';modules.mkdir()
            for name in ('main','sdk','subsdk0'):
                for suffix in SUFFIXES:(modules/(name+'.'+suffix)).write_bytes(b'')
            (modules/'main.patched.bin').write_bytes(image)
            (modules/'main.fixups.tsv').write_text('# BUILD_ID db0507f36b84200ac05f75d76ed5026f\n# LINUX_TLS_OFFSET_IDX 8\n# KEY_PAGE_OFF 0x3214000\n# PATCH_OFF 0x3220000\n# TEXT_END 0x1b786a0\n# RO_OFF 0x1b79000\n# DATA_OFF 0x2056000\n')
            with self.assertRaises(ValueError):prepare(modules,root/'mixed','cc')
            report=prepare(modules,root/'new','cc',256,320,250,game_build='94012')
            self.assertTrue(report['input_unchanged'])
            self.assertEqual(len(report['sites']),3)
            self.assertTrue(report['classic_graphics_policy']['game_era_unchanged'])
            verify_classic_modules(root/'new')
            output=(root/'new/main.patched.bin').read_bytes()
            self.assertEqual(output[0xb15de8:0xb15dec],bytes.fromhex('1f2003d5'))
            self.assertEqual(image[0xb15de8:0xb15dec],bytes.fromhex('28020054'))
            for mask in range(8):
                for blend in (0,0x80000000,0x3f800000):
                    for site in report['sites']:
                        calls=execute_stub(output,site,mask,blend,report['control_offset'],0x2dc0758)
                        self.assertEqual(calls,[] if blend==0 and mask&site['mask_bit'] else [site['target']])


class RuntimeBuildIdentity(unittest.TestCase):
    def test_read_only_offsets_do_not_misidentify_a_new_runtime_as_old(self):
        from classic_bundle import runtime_game_build
        self.assertEqual(runtime_game_build(b'legacy image'), '90447')
        new=b'old audit c5e0c2db39a8d8a3ae2d5dfaaa68e16e\0D2R-LINUX-GAME-BUILD=94012\0'
        self.assertEqual(runtime_game_build(new),'94012')
        for invalid in (new+b'D2R-LINUX-GAME-BUILD=90447\0',b'D2R-LINUX-GAME-BUILD=99999\0'):
            with self.assertRaises(ValueError):runtime_game_build(invalid)
