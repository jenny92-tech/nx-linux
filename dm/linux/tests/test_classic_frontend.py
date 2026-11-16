# INPUT: Synthetic layout cameras/buttons and private-overlay identity fixtures.
# OUTPUT: Camera edits preserve unrelated UI; unreviewed activation fails closed.
# POS: Host-only frontend preparation tests; no game or device input required.
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from prepare_classic_frontend import character_cameras, character_scene, light_creation_scene, fit_character_options, fit_character_stats, light_menu_player, disable_cameras, without_class_preloads, PRELOAD_LAYOUT_SOURCES, LAYOUT_SHA256, POLICY, CHARACTER_POLICY
from install_game import verify_classic_frontend
import casc_overlay as casc


class ClassicFrontendTests(unittest.TestCase):
    def test_light_creation_keeps_authored_lighting_and_sound_without_environment_preloads(self):
        light = dict(id=1, components=[dict(type='TransformDefinitionComponent', position=[1050, 3, 120]),
            dict(type='SpotLightDefinitionComponent', power=65, color=[1,.5,.1], lightMask=1)])
        sound = dict(id=2, components=[dict(type='AudioEmitterComponent', sound='original campfire')])
        scene = dict(type='Preset', entities=[light, sound,
            dict(id=3, components=[dict(type='ModelDefinitionComponent', filename='scenery.model')]),
            dict(id=4, components=[dict(type='VfxDefinitionComponent', filename='fog.particles')])],
            dependencies=dict(models=[dict(path='scenery.model')], textures=[dict(path='scenery.texture')],
                              particles=[dict(path='fog.particles')], json=[dict(path='original biome.json')]),
            biomeFilename='original biome.json', specialTiles={})
        before = copy.deepcopy(scene)
        result, report = light_creation_scene(scene)
        self.assertEqual(result['entities'], [light, sound])
        self.assertEqual(result['dependencies'], dict(models=[], textures=[], particles=[], json=scene['dependencies']['json']))
        self.assertEqual(result['biomeFilename'], scene['biomeFilename'])
        self.assertEqual(report['authored_lights_preserved'], 1)
        self.assertEqual(scene, before)
        light['components'].append(dict(type='ModelDefinitionComponent'))
        with self.assertRaises(ValueError): light_creation_scene(scene)

    def test_controller_character_sheet_fits_narrow_handheld_without_control_changes(self):
        source = dict(basedOn='CharacterStatsPanelHD.json', type='CharacterStatsPanel',
                      fields=dict(rect='$CharacterStatsPanelRect', defaultWidget=['StrengthPointButton']),
                      children=[dict(name='FireText', fields=dict(text='-100%', rect=dict(x=2481, width=197))),
                                dict(name='StrengthPointButton', fields=dict(onClickMessage='AddStrength'))])
        result = json.loads(fit_character_stats(json.dumps(source).encode()))
        rect = result['fields'].pop('rect')
        for canvas_width in (2880, 3456, 3840):  # 4:3, 16:10, 16:9 at 2160 logical height
            self.assertGreaterEqual(rect['x'], -canvas_width/2)
            self.assertLessEqual(rect['x'] + rect['width']*rect['scale'], canvas_width/2)
        result['fields']['rect'] = '$CharacterStatsPanelRect'
        self.assertEqual(result, source)
        for value in (b'{}', b'{"rect":"$CharacterStatsPanelRect","duplicate":{"rect":"$CharacterStatsPanelRect"}}'):
            with self.assertRaises(ValueError): fit_character_stats(value)

    def test_selected_player_survives_removing_base_and_inherited_preloads(self):
        self.assertEqual(len(PRELOAD_LAYOUT_SOURCES), 3)
        names = ('Amazon', 'Sorceress', 'Necromancer', 'Paladin', 'Barbarian', 'Druid', 'Assassin')
        for game_build in ('90447', '94012'):
            selected_names = names + (('Warlock',) if game_build == '94012' else ())
            for newline in ('\n', '\r\n'):
                for inherited in (False, True):
                    blocks = []
                    for name in selected_names:
                        fields = '' if inherited else '            "fields": {"playerClass": "%s"},\n' % name
                        blocks.append('        {\n            "type": "PlayerLoaderWidget", "name": "Preload%s",\n%s        },\n' % (name, fields))
                    selected = '        {"type": "PlayerLoaderWidget", "name": "Player", "fields": {"animationSpeed": 0}},\n'
                    controls = '        {"type": "ButtonWidget", "name": "Play", "fields": {"onClickMessage": "PlayGame"}}\n'
                    source = ('{"children": [\n' + ''.join(blocks) + selected + controls + ']}\n').replace('\n', newline).encode()
                    expected = ('{"children": [\n' + selected + controls + ']}\n').replace('\n', newline).encode()
                    self.assertEqual(without_class_preloads(source,game_build), expected)
                    with self.assertRaises(ValueError):
                        without_class_preloads(source.replace(b'PreloadAmazon', b'PreloadUnknown'),game_build)
                    with self.assertRaises(ValueError):
                        without_class_preloads(source.replace(b'PreloadAmazon', b'PreloadSorceress'),game_build)
                    with self.assertRaises(ValueError):
                        without_class_preloads(source.replace(b'"name": "Player"', b'"name": "Other"'),game_build)

    def test_mode_cards_and_text_fit_4_by_3_without_changing_actions(self):
        cards=[]
        for y in (493,899,1306):
            for x in (-1601,173):
                text_x=50 if x<0 else 160
                cards.append(dict(type='ToggleButtonWidget',fields=dict(rect=dict(x=x,y=y,width=1471,height=360),
                    onClickMessage='original mode selection',navigation=dict(up='original next widget')),
                    children=[dict(type='TextBoxWidget',fields=dict(rect=dict(x=text_x,y=40,width=690,height=60),text='original title',style='original font')),
                              dict(type='TextBoxWidget',fields=dict(rect=dict(x=text_x,y=150,width=1255,height=175),text='original description',style='original font'))]))
        source=dict(children=cards);before=copy.deepcopy(source)
        result=json.loads(fit_character_options(json.dumps(source,indent=2).encode()))
        for old,new in zip(cards,result['children']):
            rect=new['fields']['rect'];self.assertGreaterEqual(rect['x'],-1440);self.assertLessEqual(rect['x']+rect['width'],1440)
            self.assertEqual({k:v for k,v in old['fields'].items() if k!='rect'},
                             {k:v for k,v in new['fields'].items() if k!='rect'})
            for old_child,new_child in zip(old['children'],new['children']):
                field=new_child['fields'];child=field['rect']
                self.assertGreaterEqual(child['x'],0);self.assertLessEqual(child['x']+child['width'],rect['width'])
                self.assertGreaterEqual(child['y'],0);self.assertLessEqual(child['y']+child['height'],rect['height'])
                self.assertEqual({k:v for k,v in old_child['fields'].items() if k!='rect'},
                                 {k:v for k,v in field.items() if k!='rect'})
        self.assertEqual(source,before)
        source['children'].pop()
        with self.assertRaises(ValueError):fit_character_options(json.dumps(source,indent=2).encode())

    def test_restored_shadows_preserve_original_camera_and_model_fields(self):
        layout=self.layout()
        layout['children'].append(dict(type='PlayerLoaderWidget',name='Player',fields=dict(animation='FrontendNeutral')))
        before=copy.deepcopy(layout)
        result=json.loads(character_cameras(json.dumps(layout,indent=2).encode(),True,True))
        self.assertEqual(result['children'][:6],layout['children'][:6])
        self.assertEqual(layout,before)
        terrain=dict(id=1,components=[dict(type='TerrainDefinitionComponent'),
            dict(type='ModelDefinitionComponent',visibleLayers=1,shadowMask=7,ghostShadows=True)])
        model=dict(id=2,components=[dict(type='ModelDefinitionComponent',shadowMask=5,ghostShadows=True)])
        scene=dict(type='Preset',entities=[terrain,model,dict(id=3,components=[dict(type='VfxDefinitionComponent')])])
        before=copy.deepcopy(scene)
        restored,proof=character_scene(scene,True,True)
        self.assertEqual(restored['entities'],[terrain,model])
        self.assertEqual(scene,before)
        self.assertEqual(proof['decoration_entities_removed'],1)

    def test_menu_fill_preserves_other_visual_data_and_rejects_unreviewed_input(self):
        original=dict(entities=[dict(components=[dict(type='VisualDataComponent',exposureValue=4.0,
            playerUnitLightPower=0.0,fogDensity=.25,gradingLutFile='original')])])
        before=copy.deepcopy(original);result,changes=light_menu_player(original)
        component=result['entities'][0]['components'][0]
        self.assertEqual(component['fogDensity'],.25);self.assertEqual(component['gradingLutFile'],'original')
        self.assertGreater(component['playerUnitLightPower'],0);self.assertEqual(len(changes),2)
        self.assertEqual(original,before)
        original['entities'][0]['components'][0]['exposureValue']=0
        with self.assertRaises(ValueError):light_menu_player(original)

    def test_replacing_a_prepared_texture_uses_its_checked_tvfs_path(self):
        name='data/test.bin';paths,_=casc.trie_bytes({name.encode():0})
        cft=b'\x22'*16+struct.pack('>I',12)+b'\0'+struct.pack('>I',24)
        vfs=b'\x01'+struct.pack('>II',0,24)+b'\0'
        po=46;co=po+len(paths);vo=co+len(cft)
        blob=bytearray(46);blob[:4]=b'TVFS';struct.pack_into('>6I',blob,12,po,len(paths),vo,len(vfs),co,len(cft))
        blob+=paths+cft+vfs
        manifest={name:['','','','','','','11'*16,'','','','']}
        self.assertEqual(casc.cft_position(blob,co,co+len(cft),manifest,name),co)
        blob[vo]=2
        with self.assertRaises(SystemExit):casc.cft_position(blob,co,co+len(cft),manifest,name)

    def test_updated_tvfs_missing_separator_requires_unique_manifest_and_path(self):
        name='data/hd/env/walls/low.texture'
        paths,_=casc.trie_bytes({b'data/hd/env/wallslow.texture':0})
        cft=b'\x22'*16+struct.pack('>I',12)+b'\0'+struct.pack('>I',24)
        vfs=b'\x01'+struct.pack('>II',0,24)+b'\0'
        po=46;co=po+len(paths);vo=co+len(cft)
        blob=bytearray(46);blob[:4]=b'TVFS';struct.pack_into('>6I',blob,12,po,len(paths),vo,len(vfs),co,len(cft))
        blob+=paths+cft+vfs
        manifest={name:['','','','','','','11'*16,'','','','']}
        self.assertEqual(casc.cft_position(blob,co,co+len(cft),manifest,name),co)
        manifest['data/hd/env/wall/slow.texture']=list(manifest[name])
        with self.assertRaises(SystemExit):casc.cft_position(blob,co,co+len(cft),manifest,name)

    def layout(self):
        cameras=[]
        for i in range(1,6):
            fields=dict(activateOnMessage='CharacterSelect:SelectAct:Act'+str(i),
                        position=[i,2,3],fovDegrees=42,enableOutdoorShadow=True,
                        overrideFrustumBoundsOnActivate=True,frustumBoundsMin=[-300,0,0],
                        frustumBoundsMax=[60,60,550],playerBoundsMin=[30,0,475],playerBoundsMax=[40,10,490])
            if i==1:fields['activateOnOpened']=True
            cameras.append(dict(type='Camera3DWidget',name='Act%dCamera'%i,fields=fields))
        return dict(type='MainMenuHDPanel',children=cameras+[
            dict(type='ButtonWidget',name='Play',fields=dict(text='Play',message='StartGame'))])

    def test_camera_activation_only_buttons_and_positions_are_preserved(self):
        data=self.layout();before=copy.deepcopy(data);raw=json.dumps(data,indent=2).encode()
        result=json.loads(disable_cameras(raw))
        for i in range(5):
            expected=copy.deepcopy(before['children'][i]);expected['fields']['activateOnMessage']=''
            if i==0:expected['fields']['activateOnOpened']=False
            self.assertEqual(result['children'][i],expected)
        self.assertEqual(result['children'][5],before['children'][5]);self.assertEqual(data,before)

    def test_unknown_missing_or_duplicate_cameras_are_rejected(self):
        for change in ('missing','duplicate','message'):
            data=self.layout()
            if change=='missing':data['children'].pop(0)
            elif change=='duplicate':data['children'][1]=copy.deepcopy(data['children'][0])
            else:data['children'][0]['fields']['activateOnMessage']='UnreviewedAction'
            with self.assertRaises(ValueError):disable_cameras(json.dumps(data,indent=2).encode())

    def test_selected_player_view_changes_only_with_explicit_option(self):
        data=self.layout();player=dict(type='PlayerLoaderWidget',name='Player',
            fields=dict(animation='FrontendNeutral',position=[1,2,3]))
        data['children'].append(player);raw=json.dumps(data,indent=2).encode()
        self.assertEqual(json.loads(disable_cameras(raw))['children'][-1],player)
        changed=json.loads(disable_cameras(raw,True))
        self.assertEqual(changed['children'][-1],dict(player,fields=dict(player['fields'],captureInView='$INT_MAX')))
        self.assertEqual(changed['children'][-2],data['children'][-2])
        with self.assertRaises(ValueError):disable_cameras(json.dumps(self.layout(),indent=2).encode(),True)

    def test_character_mode_keeps_player_camera_activation_and_all_controls(self):
        data=self.layout();player=dict(type='PlayerLoaderWidget',name='Player',fields=dict(animation='FrontendNeutral'))
        data['children'].append(player)
        result=json.loads(character_cameras(json.dumps(data,indent=2).encode()))
        for i in range(5):
            expected=copy.deepcopy(data['children'][i]);fields=expected['fields']
            fields.update(frustumBoundsMin=fields['playerBoundsMin'],frustumBoundsMax=fields['playerBoundsMax'],
                          enableOutdoorShadow=False)
            self.assertEqual(result['children'][i],expected)
        self.assertEqual(result['children'][5],data['children'][5])
        self.assertEqual(result['children'][-1],dict(player,fields=dict(player['fields'],animationSpeed=0)))
        self.assertNotIn('captureInView',result['children'][-1]['fields'])

    def test_character_mode_rejects_hidden_player_and_unreviewed_bounds(self):
        data=self.layout();data['children'].append(dict(type='PlayerLoaderWidget',name='Player',fields=dict(animation='FrontendNeutral')))
        for change in ('hidden','missing_bounds','bad_activation'):
            candidate=copy.deepcopy(data)
            if change=='hidden':candidate['children'][-1]['fields']['captureInView']='$INT_MAX'
            elif change=='missing_bounds':del candidate['children'][0]['fields']['playerBoundsMin']
            else:candidate['children'][0]['fields']['activateOnOpened']=False
            with self.assertRaises(ValueError):character_cameras(json.dumps(candidate,indent=2).encode())

    def test_background_mode_preserves_original_camera_coverage(self):
        data=self.layout();data['children'].append(dict(type='PlayerLoaderWidget',name='Player',fields=dict(animation='FrontendNeutral')))
        result=json.loads(character_cameras(json.dumps(data,indent=2).encode(),True))
        for index in range(5):
            expected=copy.deepcopy(data['children'][index]);expected['fields']['enableOutdoorShadow']=False
            self.assertEqual(result['children'][index],expected)

    def test_background_mode_keeps_scenery_and_terrain_without_dynamic_decoration(self):
        terrain=dict(id=1,components=[dict(type='TerrainDefinitionComponent'),
            dict(type='ModelDefinitionComponent',visibleLayers=1,filename='terrain',shadowMask=3)])
        tree=dict(id=2,components=[dict(type='ModelDefinitionComponent',filename='tree',visibleLayers=1,shadowMask=7)])
        scene=dict(type='Preset',dependencies=['original'],entities=[terrain,tree,
            dict(id=3,components=[dict(type='VfxDefinitionComponent')]),
            dict(id=4,components=[dict(type='PointLightDefinitionComponent')])])
        before=copy.deepcopy(scene);result,proof=character_scene(scene,True)
        self.assertEqual([e['id'] for e in result['entities']],[1,2,4])
        self.assertEqual(result['entities'][0]['components'][1]['visibleLayers'],1)
        self.assertEqual(result['entities'][1]['components'][0]['filename'],'tree')
        self.assertEqual(result['entities'][1]['components'][0]['shadowMask'],0)
        self.assertTrue(proof['static_background_preserved']);self.assertEqual(scene,before)
        self.assertEqual(result['dependencies'],scene['dependencies'])

    def test_scene_cut_removes_authored_visuals_but_keeps_root_and_audio(self):
        terrain=dict(name='terrain',id=1,components=[dict(type='TerrainDefinitionComponent'),
            dict(type='ModelDefinitionComponent',visibleLayers=1,filename='original terrain')])
        audio=dict(name='audio',id=2,components=[dict(type='AudioEmitterComponent',event='original ambience')])
        scene=dict(type='Preset',terrain={'width':123},dependencies={'models':['unchanged']},entities=[terrain,audio,
            dict(id=3,components=[dict(type='ModelDefinitionComponent',visibleLayers=1)]),
            dict(id=4,components=[dict(type='VfxDefinitionComponent')])])
        result,proof=character_scene(scene)
        self.assertEqual(result['entities'][1],audio);self.assertEqual(len(result['entities']),2)
        self.assertEqual(result['entities'][0]['components'][1]['visibleLayers'],0)
        self.assertEqual(scene['entities'][0]['components'][1]['visibleLayers'],1)
        self.assertEqual(result['terrain'],scene['terrain']);self.assertEqual(result['dependencies'],scene['dependencies'])
        self.assertEqual(proof['decoration_entities_removed'],2)

    def test_installer_binds_frontend_to_source_pack_and_rejects_payload_tamper(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);pack=root/'pack';delta=root/'delta'
            (pack/'RomFS/data').mkdir(parents=True);(delta/'overlay/data').mkdir(parents=True)
            source=pack/'RomFS/data/00-00000007.data';source.write_bytes(b'original prepared payload')
            entry=lambda p,n:dict(path=n,size=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
            source_entry=entry(source,'RomFS/data/00-00000007.data')
            manifest=pack/'manifest.json';manifest.write_text(json.dumps(dict(files=[source_entry])))
            config=delta/'overlay/data/.build.config';config.write_bytes(b'config')
            data=delta/'overlay/data/00-0000000a.data';data.write_bytes(b'menu payload')
            files={str(p.relative_to(delta)):entry(p,str(p.relative_to(delta))) for p in (config,data)}
            report=dict(schema=1,complete=True,game_build='90447',policy=POLICY,no_preview=True,
                source_layout_sha256=LAYOUT_SHA256,roundtrip_verified=True,output_files=files,
                source_prepared_manifest_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest(),
                source_prepared_files={source_entry['path']:source_entry})
            (delta/'complete.json').write_text(json.dumps(report))
            self.assertEqual(verify_classic_frontend(delta,pack),delta/'overlay')
            report.update(policy=CHARACTER_POLICY,no_preview=False,player_capture_disabled=False,tight_character_bounds=True,background_scene_cut=True)
            (delta/'complete.json').write_text(json.dumps(report))
            self.assertEqual(verify_classic_frontend(delta,pack),delta/'overlay')
            report.update(policy='classic-frontend-character-v3',tight_character_bounds=False,
                          background_scene_cut=False,static_background_preserved=True)
            (delta/'complete.json').write_text(json.dumps(report))
            self.assertEqual(verify_classic_frontend(delta,pack),delta/'overlay')
            report['static_background_preserved']=False
            (delta/'complete.json').write_text(json.dumps(report))
            with self.assertRaises(ValueError):verify_classic_frontend(delta,pack)
            report['static_background_preserved']=True
            report['player_capture_disabled']=True
            (delta/'complete.json').write_text(json.dumps(report))
            with self.assertRaises(ValueError):verify_classic_frontend(delta,pack)
            report['player_capture_disabled']=False
            (delta/'complete.json').write_text(json.dumps(report))
            manifest.write_text(json.dumps(dict(game_build='94012', files=[source_entry])))
            report.update(policy='classic-frontend-character-v6',game_build='94012',
                          source_prepared_manifest_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest(),
                          source_layout_sha256='2380b578085b918123d55be8731069f9446f85032e0eeb7716d8364a2db59b79',
                          tight_character_bounds=True,background_scene_cut=True,static_background_preserved=False,
                          selected_character_only=True,handheld_mode_layout=True,handheld_character_sheet=True,light_creation=True)
            (delta/'complete.json').write_text(json.dumps(report))
            self.assertEqual(verify_classic_frontend(delta,pack),delta/'overlay')
            report['light_creation']=False
            (delta/'complete.json').write_text(json.dumps(report))
            with self.assertRaises(ValueError):verify_classic_frontend(delta,pack)
            report['light_creation']=True
            (delta/'complete.json').write_text(json.dumps(report))
            data.write_bytes(b'MENU payload')
            with self.assertRaises(ValueError):verify_classic_frontend(delta,pack)

    def test_menu_overlay_requires_classic_mode_before_reading_private_paths(self):
        tool=Path(__file__).resolve().parents[1]/'install_game.py'
        args=[sys.executable,str(tool),'--game-root','/missing/game','--runtime-root','/missing/runtime',
              '--profile-root','/missing/profile','--controllers','/missing/controllers','--entry','/missing/entry',
              '--classic-frontend','/missing/frontend']
        result=subprocess.run(args,capture_output=True,text=True)
        self.assertEqual(result.returncode,2);self.assertIn('--classic-frontend requires --classic30',result.stderr)


if __name__=='__main__':unittest.main()
