# INPUT: Synthetic authored terrain/light data, including unrelated game fields.
# OUTPUT: Population/shadow/bloom cuts with original objects and data preserved.
# POS: Host-only quality transformation checks; no proprietary assets.
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_remaster_quality import cut_biome, cut_visual, strict_json
from install_game import verify_remaster_quality


class RemasterQualityTests(unittest.TestCase):
    def test_density_cut_preserves_ground_assets_and_all_unrelated_properties(self):
        layer=dict(GrassDensity=2.0, ClutterDensity=.5, GrassColor=[1, .4, .2],
                   Clutter=[dict(ModelFilename='grass.model', Density=.2)], Collision='keep')
        original={k:dict(terrainLayers=[copy.deepcopy(layer)]) for k in
                  ('terrainDataLow','terrainDataMed','terrainDataHigh','terrainDataUltra')}
        original['type']='Biome'
        original['other']=dict(GrassDensity=12, Density=7)
        before=copy.deepcopy(original)
        result,changes=cut_biome(original,0)
        self.assertEqual(original,before)
        self.assertEqual(len(changes),8)
        for k in original:
            if k in ('other','type'):continue
            expected=dict(layer,GrassDensity=0.0,ClutterDensity=0.0)
            self.assertEqual(result[k]['terrainLayers'],[expected])
        self.assertEqual(result['other'],before['other'])

    def test_half_density_is_a_population_change_only(self):
        original=dict(type='Biome',terrainDataLow=dict(terrainLayers=[dict(GrassDensity=.8,ClutterDensity=.2)]))
        result,_=cut_biome(original,.5)
        self.assertEqual(result['terrainDataLow']['terrainLayers'][0],dict(GrassDensity=.4,ClutterDensity=.1))
        self.assertEqual(cut_biome(original,1),(original,[]))

    def test_visual_cuts_preserve_lights_actors_and_exposure(self):
        original=dict(type='VisualDataDefinition',entities=[dict(components=[
            dict(type='DirectionalLightDefinitionComponent',shadowCaster=True,intensity=3.0),
            dict(type='VisualDataComponent',bloomIntensity=1.5,exposure=2.0),
            dict(type='UnrelatedActor',shadowCaster=True,bloomIntensity=2.0)])],actors=['keep'])
        before=copy.deepcopy(original)
        result,changes=cut_visual(original,True,True)
        self.assertEqual(original,before)
        self.assertEqual(len(changes),2)
        self.assertEqual(result['entities'][0]['components'],[
            dict(type='DirectionalLightDefinitionComponent',shadowCaster=False,intensity=3.0),
            dict(type='VisualDataComponent',bloomIntensity=0.0,exposure=2.0),
            dict(type='UnrelatedActor',shadowCaster=True,bloomIntensity=2.0)])
        self.assertEqual(result['actors'],['keep'])
        self.assertEqual(cut_visual(original,False,False),(original,[]))

    def test_unreviewed_schema_and_nonfinite_or_boolean_densities_are_rejected(self):
        for value in (True,-1,float('nan'),'0'):
            with self.assertRaises(ValueError):
                cut_biome(dict(type='Biome',terrainDataLow=dict(terrainLayers=[dict(GrassDensity=value)])),0)
        for value in (-.1,1.1,float('nan'),True):
            with self.assertRaises(ValueError):cut_biome({},value)
        with self.assertRaises(ValueError):cut_visual(dict(type='VisualDataDefinition',entities=[
            dict(components=[dict(type='DirectionalLightDefinitionComponent',shadowCaster=1)])]),True,False)

    def test_duplicate_and_nonfinite_json_cannot_silently_change_other_fields(self):
        for raw in ('{"x":1,"x":2}','{"x":NaN}'):
            with self.assertRaises(ValueError):strict_json(raw)

    def test_installer_binds_overlay_to_exact_source_pack_and_payloads(self):
        def entry(path,data):
            return dict(path=path,size=len(data),sha256=hashlib.sha256(data).hexdigest())
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);pack=root/'pack';delta=root/'delta'
            (pack/'RomFS/data').mkdir(parents=True);(delta/'overlay/data').mkdir(parents=True)
            source=pack/'RomFS/data/base.data';source.write_bytes(b'base')
            manifest=dict(source_manifest_sha256='original90447',files=[entry('RomFS/data/base.data',b'base')])
            (pack/'manifest.json').write_text(json.dumps(manifest))
            files={}
            for name,data in (('overlay/data/.build.config',b'config'),('overlay/data/00-0000000a.data',b'visuals')):
                (delta/name).write_bytes(data);files[name]=entry(name,data)
            report=dict(schema=1,game_build='90447',complete=True,policy='remaster-authored-visual-cuts-v1',
                        roundtrip_verified=True,source_manifest_sha256='original90447',output_files=files,
                        source_prepared_manifest_sha256=hashlib.sha256((pack/'manifest.json').read_bytes()).hexdigest())
            receipt=delta/'complete.json';receipt.write_text(json.dumps(report))
            self.assertEqual(verify_remaster_quality(delta,pack),delta/'overlay')
            source.write_bytes(b'bad!')
            with self.assertRaisesRegex(ValueError,'checksum'):verify_remaster_quality(delta,pack)
            source.write_bytes(b'base');(delta/'overlay/data/00-0000000a.data').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'checksum'):verify_remaster_quality(delta,pack)
            report['source_prepared_manifest_sha256']='wrong';receipt.write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError,'another base'):verify_remaster_quality(delta,pack)

    def test_classic_and_remaster_modes_are_rejected_before_any_installation(self):
        tool=Path(__file__).resolve().parents[1]/'install_game.py'
        args=[sys.executable,str(tool),'--classic30','--remaster3d']
        for flag in ('--game-root','--runtime-root','--profile-root','--controllers','--entry'):
            args += [flag,'/unused-remaster-test']
        result=subprocess.run(args,capture_output=True,text=True)
        self.assertEqual(result.returncode,2)
        self.assertIn('not allowed with argument',result.stderr)


if __name__=='__main__':unittest.main()
