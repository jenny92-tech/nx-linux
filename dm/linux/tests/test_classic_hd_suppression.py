# INPUT: Synthetic visual definitions with resource, identity and preload fields.
# OUTPUT: Omitted dependency proofs and rejection of unreviewed asset schemas.
# POS: Host-only regression checks for the experimental classic resource overlay.
import copy
import json
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_classic_hd_suppression import suppress_definition, suppress_menu_loaders
from install_game import verify_classic_hd_suppression


def definition(kind):
    return dict(type=kind, name='test', dependencies=dict(models=[dict(path='data/hd/test.model')],
        textures=[dict(path='data/hd/test.texture')], json=[]), entities=[])


def entity(*components):
    return dict(type='Entity', name='root', id=123, components=list(components))


def menu():
    children = [dict(type='Camera3DWidget', name='Act%dCamera' % i) for i in range(1, 6)]
    children += [dict(type='PlayerMoverWidget', name='Act%dPlayer' % i) for i in range(1, 6)]
    children += [dict(type='PlayerLoaderWidget', name='Preload' + name) for name in
                 ('Amazon', 'Sorceress', 'Necromancer', 'Paladin', 'Barbarian', 'Druid', 'Assassin')]
    children.append(dict(type='PlayerLoaderWidget', name='Player'))
    controls = [dict(type=kind, name=name, fields={'text': 'literal // /* */ ,}'}) for kind, name in
                (('ButtonWidget', 'QuickPlayButton'), ('ImageWidget', 'Divider'),
                 ('ButtonWidget', 'SecondaryContextButton'), ('RectangleWidget', 'PlayerInfoContainer'))]
    return dict(type='MainMenuHDPanel', name='MainMenuPanel', fields={}, children=children + controls)


class SuppressionTests(unittest.TestCase):
    def test_menu_omits_loaders_and_preserves_ui_strings_comments_and_controls(self):
        original = menu()
        raw = json.dumps(original)[:-1] + ', /* comment before final brace */ } // trailing comment\n'
        updated, removed = suppress_menu_loaders(raw.encode())
        expected = copy.deepcopy(original)
        expected['children'] = expected['children'][-4:]
        self.assertEqual(json.loads(updated), expected)
        self.assertEqual(len(removed), 18)
        self.assertEqual(sum(row['type'] == 'PlayerLoaderWidget' for row in removed), 8)

    def test_menu_rejects_unknown_missing_duplicate_widgets_and_duplicate_json_keys(self):
        for mutation in ('unknown', 'missing', 'duplicate'):
            original = menu()
            if mutation == 'unknown':
                original['children'][0]['name'] = 'UnreviewedCamera'
            elif mutation == 'missing':
                original['children'].pop()
            else:
                original['children'].append(copy.deepcopy(original['children'][0]))
            with self.assertRaises(ValueError):
                suppress_menu_loaders(json.dumps(original).encode())
        with self.assertRaisesRegex(ValueError, 'duplicate asset JSON key'):
            suppress_menu_loaders(b'{"type":"MainMenuHDPanel","type":"Other"}')

    def test_unit_identity_retained_with_no_visual_loads_and_no_input_mutation(self):
        original = definition('UnitDefinition')
        root = dict(type='UnitRootComponent', name='unit', state_machine_filename='data/hd/state.json',
                    onCreateEventName='create_fx', animations=[dict(filename='data/hd/test.animations')],
                    doNotUseHDHeight=False)
        transform = dict(type='TransformDefinitionComponent', position=dict(x=2, y=3, z=4))
        original['entities'] = [entity(root, transform, dict(type='SkeletonDefinitionComponent',
                                     filename='data/hd/test.skeleton')),
                                entity(dict(type='ModelDefinitionComponent', filename='data/hd/test.model'))]
        before = copy.deepcopy(original)
        result = suppress_definition(original)
        self.assertEqual(original, before)
        self.assertEqual(result['dependencies'], dict(models=[], textures=[], json=[]))
        self.assertEqual(len(result['entities']), 1)
        self.assertEqual(result['entities'][0]['id'], 123)
        self.assertEqual(result['entities'][0]['components'][1], transform)
        empty = result['entities'][0]['components'][0]
        self.assertEqual((empty['state_machine_filename'], empty['onCreateEventName'], empty['animations']), ('', '', []))

    def test_scene_preserves_terrain_identity_without_visual_or_biome_paths(self):
        original = definition('Preset')
        original.update(terrain=entity(dict(type='TerrainDefinitionComponent'),
                                       dict(type='ModelDefinitionComponent', filename='data/hd/terrain.model')),
                        biomeFilename='data/hd/biome.json', perTileBiomeOverrides=[dict(filename='data/hd/tile.json')],
                        specialTiles={'marker': 'data/hd/env/special/tile.json'})
        result = suppress_definition(original)
        self.assertEqual(result['terrain'], entity(dict(type='TerrainDefinitionComponent')))
        self.assertEqual(result['entities'], [])
        self.assertEqual(result['biomeFilename'], '')
        self.assertEqual(result['perTileBiomeOverrides'], [])
        self.assertEqual(result['specialTiles'], {})
        for empty in ({}, None):
            original['terrain'] = empty
            self.assertEqual(suppress_definition(original)['terrain'], empty)

    def test_original_empty_overlay_and_prefab_roots_are_kept(self):
        for kind in ('OverlayDefinition', 'Prefab'):
            original = definition(kind)
            original['entities'] = [entity(dict(type='VfxDefinitionComponent', filename='data/hd/fx.particles'))]
            self.assertEqual(suppress_definition(original)['entities'], [entity()])

    def test_act_preload_entries_are_removed_without_dropping_categories(self):
        original = definition('UnitDefinitionPreloadFile')
        for key in ('players', 'monsters', 'objects', 'missiles', 'items', 'roomtiles'):
            original[key] = [dict(filename='data/hd/unit.json')]
        result = suppress_definition(original)
        for key in ('players', 'monsters', 'objects', 'missiles', 'items', 'roomtiles'):
            self.assertEqual(result[key], [])
        original['players'] = 'unreviewed'
        with self.assertRaisesRegex(ValueError, 'preload list'):
            suppress_definition(original)

    def test_unknown_schema_or_leftover_resource_fails_closed(self):
        original = definition('Preset')
        original['unexpectedLoader'] = 'data/hd/retained.model'
        with self.assertRaisesRegex(ValueError, 'residual visual resource'):
            suppress_definition(original)
        original = definition('OverlayDefinition')
        original['dependencies']['models'] = 'data/hd/retained.model'
        with self.assertRaisesRegex(ValueError, 'dependency-list'):
            suppress_definition(original)
        with self.assertRaisesRegex(ValueError, 'unsupported visual'):
            suppress_definition(definition('GameplayTranslationSettings'))

    def test_missing_or_duplicate_unit_root_is_rejected(self):
        original = definition('UnitDefinition')
        with self.assertRaisesRegex(ValueError, 'one unit root'):
            suppress_definition(original)
        original['entities'] = [entity(dict(type='UnitRootComponent'), dict(type='UnitRootComponent'))]
        with self.assertRaisesRegex(ValueError, 'one unit root'):
            suppress_definition(original)


class SuppressionInstallTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root=Path(self.temporary.name)
        self.prepared=root/'prepared';self.directory=root/'suppression'
        def add(base,name,data):
            path=base/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
            return dict(size=len(data),sha256=hashlib.sha256(data).hexdigest())
        source_name='RomFS/data/00-00000001.data'
        proof=add(self.prepared,source_name,b'prepared assets')
        manifest=dict(source_manifest_sha256='a'*64,files=[dict(path=source_name,**proof)])
        manifest_bytes=json.dumps(manifest).encode()
        add(self.prepared,'manifest.json',manifest_bytes)
        files={name:add(self.directory,name,data) for name,data in
               (('overlay/data/.build.config',b'config'),('overlay/data/00-00000008.data',b'frontend'),
                ('overlay/data/00-0000000a.data',b'definitions'))}
        inherited={name:proof for name,proof in files.items() if not name.endswith('0a.data')}
        self.report=dict(schema=1,complete=True,game_build='90447',policy='classic-hd-suppression-experiment-v1',
            roundtrip_verified=True,scope='all',source_overlay_kind='frontend',inherited_pixel_textures=0,
            classic_only=True,hd_visual_definition_references_removed=True,classic_sprites_changed=False,
            ui_sprites_changed=False,fonts_changed=False,gameplay_tables_changed=False,original_files_modified=False,
            source_manifest_sha256='a'*64,source_prepared_manifest_sha256=hashlib.sha256(manifest_bytes).hexdigest(),
            output_files=files,source_frontend_files=inherited,source_overlay_files=copy.deepcopy(inherited))
        self.save_report()

    def save_report(self):
        (self.directory/'complete.json').write_text(json.dumps(self.report))

    def test_verified_hex_archive_and_carried_frontend_are_accepted(self):
        self.assertEqual(verify_classic_hd_suppression(self.directory,self.prepared),self.directory/'overlay')

    def test_failed_pixel_variant_and_other_base_pack_are_rejected(self):
        self.report['source_overlay_kind']='pixel';self.save_report()
        with self.assertRaisesRegex(ValueError,'original-size'):
            verify_classic_hd_suppression(self.directory,self.prepared)
        self.report['source_overlay_kind']='frontend';self.report['source_prepared_manifest_sha256']='b'*64
        self.save_report()
        with self.assertRaisesRegex(ValueError,'source identity'):
            verify_classic_hd_suppression(self.directory,self.prepared)

    def test_corrupted_inherited_asset_or_extra_payload_is_rejected(self):
        path=self.directory/'overlay/data/00-00000008.data'
        path.write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError,'checksum'):
            verify_classic_hd_suppression(self.directory,self.prepared)
        path.write_bytes(b'frontend')
        (self.directory/'overlay/data/unreported.data').write_bytes(b'extra')
        with self.assertRaisesRegex(ValueError,'unexpected'):
            verify_classic_hd_suppression(self.directory,self.prepared)

    def test_escaping_symlink_and_modified_classic_payload_claim_are_rejected(self):
        path=self.directory/'overlay/data/00-0000000a.data'
        payload=path.read_bytes();path.unlink()
        outside=Path(self.temporary.name)/'outside.data';outside.write_bytes(payload);path.symlink_to(outside)
        with self.assertRaisesRegex(ValueError,'checksum'):
            verify_classic_hd_suppression(self.directory,self.prepared)
        path.unlink();path.write_bytes(payload)
        self.report['classic_sprites_changed']=True;self.save_report()
        with self.assertRaisesRegex(ValueError,'original-size'):
            verify_classic_hd_suppression(self.directory,self.prepared)


if __name__ == '__main__':
    unittest.main()
