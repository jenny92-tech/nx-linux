# INPUT: Reviewed model profiles, synthetic component reports and private bundle descriptors.
# OUTPUT: Model routing and profile/component mismatches are checked without real game data.
# POS: Desktop release regressions; no hardware capability is inferred from a folder name.
import copy
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from classic_bundle import DEFAULTS, PATHS, descriptor, load_bundle
from device_profiles import DEVICE_IDS, install_paths, launcher_options, load_device, verify_components
from device_release import release_paths
from prepare_classic import main as prepare_main, TOOL_NAMES


class DeviceProfileTests(unittest.TestCase):
    def test_device_paths_and_saves_are_separate(self):
        profiles=[load_device(name) for name in DEVICE_IDS]
        paths=[install_paths(p) for p in profiles]
        for key in ('runtime_root','profile_root','entry','game_root'):
            self.assertNotEqual(paths[0][key],paths[1][key])
        self.assertEqual(profiles[0]['memory_mib'],4096)
        self.assertEqual(profiles[1]['memory_mib'],2048)
        self.assertIs(profiles[0]['settings']['keep_character'],True)
        self.assertIs(profiles[1]['settings']['keep_character'],False)
        with self.assertRaises(ValueError):load_device('../rk3566-2gb')
        with self.assertRaises(ValueError):load_device('unreviewed-model')
        self.assertEqual(launcher_options(profiles[1])['D2R_SHARED_POOL_MAX_MB'],256)
        damaged=copy.deepcopy(profiles[1]);damaged['settings']['texture_pool_mib']=224
        with self.assertRaises(ValueError):install_paths(damaged)

    def test_descriptor_binds_device_and_rejects_altered_settings(self):
        with tempfile.TemporaryDirectory() as temp:
            game=Path(temp);profile=load_device('rk3566-2gb')
            (game/'classic-bundle.json').write_text(json.dumps(descriptor(profile)))
            (game/'device-profile.json').write_text(json.dumps(profile))
            (game/PATHS['settings']).write_text(json.dumps(DEFAULTS))
            required={'classic-bundle.json','device-profile.json',PATHS['settings'],PATHS['shaders'],
                      PATHS['modules']+'/classic-native.json',PATHS['frontend']+'/complete.json'}
            manifest={'files':dict.fromkeys(required,{})}
            self.assertEqual(load_bundle(game,manifest)['_device'],profile)
            profile['settings']['menu_background']=True
            (game/'device-profile.json').write_text(json.dumps(profile))
            with self.assertRaises(ValueError):load_bundle(game,manifest)

    def test_mixed_old_components_cannot_claim_the_device_preset(self):
        profile=load_device('rk3566-2gb')
        native=dict(graphics_pool_policy=dict(buffer_pool_mib=256,texture_pool_mib=320),
                    cpu_pool_policy=dict(cpu_pool_mib=250),terrain_quality_policy=dict(automatic=True),
                    resource_load_policy=dict(policy='classic-resource-gate-90447-v3'))
        frontend=dict(policy='classic-frontend-no-preview-v3')
        verify_components(profile,native,frontend)
        for field in native:
            broken=copy.deepcopy(native);broken.pop(field)
            with self.assertRaises(ValueError):verify_components(profile,broken,frontend)
        broken=copy.deepcopy(native);broken['graphics_pool_policy']['texture_pool_mib']=224
        with self.assertRaises(ValueError):verify_components(profile,broken,frontend)
        with self.assertRaises(ValueError):verify_components(profile,native,dict(policy='classic-frontend-character-v4'))

    def test_release_layout_separates_public_and_private(self):
        paths=release_paths('/tmp/d2r-release-test','rk3566-2gb')
        self.assertEqual(paths['root'].name,'rk3566-2gb')
        self.assertTrue(paths['preparation'].is_relative_to(paths['private']))
        self.assertTrue(paths['software'].is_relative_to(paths['public']))
        self.assertFalse(paths['software'].is_relative_to(paths['private']))

    def test_device_check_selects_the_correct_gate_without_writing(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);inputs=root/'input';tools=root/'tools';inputs.mkdir();tools.mkdir()
            for name in ('base.nsp','update.nsp','prod.keys'):(inputs/name).write_bytes(b'synthetic fixture')
            for name in TOOL_NAMES.values():
                path=tools/name;path.write_text('#!/bin/sh\nexit 0\n');path.chmod(0o755)
            for device_id in DEVICE_IDS:
                out=root/device_id;stream=io.StringIO()
                argv=['prepare_classic.py','--device',device_id,'--input',str(inputs),'--out',str(out),'--tools-bin',str(tools),'--check']
                if device_id=='miniloong-pocket-one-rk3566-4gb':argv+=['--resources','full']
                with patch.object(sys,'argv',argv),contextlib.redirect_stdout(stream):prepare_main()
                report=json.loads(stream.getvalue());settings=load_device(device_id)['settings']
                self.assertEqual(report['pool_capacity_mib'],dict(buffer=256,texture=320,cpu=250))
                self.assertIs(report['classic_resource_gate'],settings['classic_resource_gate'])
                self.assertIs(report['classic_frontend_resources'],settings['classic_frontend_resources'])
                self.assertFalse(out.exists())
            argv=['prepare_classic.py','--input',str(inputs),'--out',str(root/'default'),
                  '--tools-bin',str(tools),'--check']
            stream=io.StringIO()
            with patch.object(sys,'argv',argv),contextlib.redirect_stdout(stream):prepare_main()
            report=json.loads(stream.getvalue())
            self.assertEqual(report['resource_bundle'],'compact')
            self.assertEqual(report['device_profile']['id'],'rk3566-2gb')
            self.assertIn('Chinese/English classic resource repack',report['stages'])


if __name__=='__main__':unittest.main()
