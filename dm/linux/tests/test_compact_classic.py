# INPUT: Classic resource categories and synthetic fixed-preset receipts.
# OUTPUT: Shared/classic/Chinese content survives, and unsafe compact claims fail.
# POS: Host regressions for the standalone resource boundary; no game files needed.
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from classic_resource_gate import PRESERVED_PREFIXES, PRESERVED_METADATA
from compact_classic import omission_reason, verify_compact, POLICY
from device_profiles import load_device


class CompactClassicTests(unittest.TestCase):
    def test_classic_ui_tables_fonts_and_selected_languages_are_retained(self):
        for name in ('data/global/chars/barbarian/hero.dcc', 'data/global/excel/weapons.bin',
                     'data/local/font/chinese.dc6', 'data/hd/ui/fonts/font.sprite',
                     'data/hd/global/ui/controller/menu.json', 'data/hd/texture_desc_cache.json',
                     'data/hd/items/items.json', 'locales/audio/zhcn/akara.opus',
                     'locales/audio/enus/akara.opus'):
            self.assertIsNone(omission_reason(name, PRESERVED_PREFIXES, PRESERVED_METADATA), name)
        for name, reason in (('data/hd/character/player/hero.model', 'native-gated-hd-world'),
                             ('data/hd/items/items.json.extra', 'native-gated-hd-world'),
                             ('data/hd/global/video/intro.webm', 'disabled-video'),
                             ('data/local/video/intro.webm', 'disabled-video'),
                             ('locales/audio/dede/akara.opus', 'unused-language'),
                             ('locales/data/jajp/large-font.sprite', 'unused-language')):
            self.assertEqual(omission_reason(name, PRESERVED_PREFIXES, PRESERVED_METADATA), reason)

    def test_missing_resources_or_observer_gate_cannot_claim_a_compact_bundle(self):
        profile = load_device('rk3566-2gb')
        with tempfile.TemporaryDirectory() as temp:
            game = Path(temp)
            (game/'classic-native-v6').mkdir(); (game/'classic-frontend').mkdir()
            native = dict(graphics_pool_policy=dict(buffer_pool_mib=256, texture_pool_mib=320),
                cpu_pool_policy=dict(cpu_pool_mib=250), terrain_quality_policy=dict(automatic=True),
                resource_load_policy=dict(policy='classic-resource-gate-90447-v3', mode=3))
            (game/'classic-native-v6/classic-native.json').write_text(json.dumps(native))
            (game/'classic-frontend/complete.json').write_text(json.dumps(dict(policy='classic-frontend-no-preview-v3')))
            resources = {name: dict(size=1, sha256='a'*64) for name in (
                'Update/RomFS/data/00-00000008.data', 'Update/RomFS/data/.build.config',
                'Update/RomFS/data/build_manifest.psv')}
            report = dict(schema=1, policy=POLICY, complete=True, game_build='90447',
                device_profile_sha256=profile['profile_sha256'], languages=['zhcn', 'enus'],
                video_playback=False, retained_encoded_bytes_unchanged=True,
                retained_entries_verified=1, requires_original_archives=False, resources=resources)
            (game/'compact-classic.json').write_text(json.dumps(report))
            manifest = dict(files={'compact-classic.json': {}, **resources})
            self.assertEqual(verify_compact(game, manifest, profile), report)
            with self.assertRaises(ValueError): verify_compact(game, dict(files={'compact-classic.json': {}}), profile)
            bad_resources = dict(resources)
            bad_resources['Update/RomFS/data/09-00000009.data'] = bad_resources.pop('Update/RomFS/data/00-00000008.data')
            (game/'compact-classic.json').write_text(json.dumps(dict(report, resources=bad_resources)))
            with self.assertRaisesRegex(ValueError, 'mounted base chunk'):
                verify_compact(game, dict(files={'compact-classic.json': {}, **bad_resources}), profile)
            (game/'compact-classic.json').write_text(json.dumps(report))
            native['resource_load_policy']['mode'] = 1
            (game/'classic-native-v6/classic-native.json').write_text(json.dumps(native))
            with self.assertRaises(ValueError): verify_compact(game, manifest, profile)


if __name__ == '__main__':
    unittest.main()
