# INPUT: Synthetic level tables and a synthetic verified overlay receipt.
# OUTPUT: Rain-only byte changes and rejection of mismatched tables/resources.
# POS: Host preprocessing/installation regression; no proprietary files required.
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from prepare_classic_weather import disable_rain
from install_game import verify_classic_weather


class WeatherTests(unittest.TestCase):
    def fixture(self):
        lines=['Name\tId\tAct\tRain\tMud\tNoPer\r\n']
        data=bytearray([0x9a])*(4+137*388);struct.pack_into('<I',data,0,137)
        rain=[]
        for i in range(137):
            values=[i//30,int(i%5==1),int(i%7==2),int(i%11==4)]
            lines.append(f'Level{i}\t{i}\t'+ '\t'.join(map(str,values))+'\r\n')
            struct.pack_into('<I',data,4+i*388,i)
            for offset,value in zip((9,11,12,13),values):data[4+i*388+offset]=value
            if values[1]:rain.append(i)
        return ''.join(lines).encode(),bytes(data),rain

    def test_only_rain_changes_with_line_endings_and_padding_preserved(self):
        text,data,rain=self.fixture();new_text,new_data,report=disable_rain(text,data)
        self.assertEqual(report['rain_rows'],rain)
        self.assertEqual([i for i,(a,b) in enumerate(zip(data,new_data)) if a!=b],[4+i*388+11 for i in rain])
        self.assertEqual(len(text),len(new_text));self.assertEqual(text.count(b'\r\n'),new_text.count(b'\r\n'))
        for old,new in zip(text.splitlines()[1:],new_text.splitlines()[1:]):
            a,b=old.split(b'\t'),new.split(b'\t');self.assertEqual(a[:3]+a[4:],b[:3]+b[4:]);self.assertEqual(b[3],b'0')

    def test_corrupt_count_id_or_field_is_rejected(self):
        text,data,_=self.fixture()
        for offset in (0,4+31*388,4+52*388+9,4+8*388+11,4+9*388+12,4+17*388+13):
            bad=bytearray(data);bad[offset]^=1
            with self.assertRaises(ValueError):disable_rain(text,bad)
        with self.assertRaises(ValueError):disable_rain(text,data[:-1])

    def test_delta_install_hashes_and_source_binding(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);pack=root/'prepared';delta=root/'delta';(pack/'RomFS/data').mkdir(parents=True);(delta/'overlay/data').mkdir(parents=True)
            base=pack/'RomFS/data/.build.config';base.write_bytes(b'synthetic-base')
            def record(p):return dict(size=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
            sources={'RomFS/data/.build.config':dict(path='RomFS/data/.build.config',**record(base))}
            (pack/'manifest.json').write_text(json.dumps(dict(files=list(sources.values()))))
            files={}
            for name in ('.build.config','00-00000008.data'):
                p=delta/'overlay/data'/name;p.write_bytes(b'synthetic-overlay');files['overlay/data/'+name]=record(p)
            receipt=dict(schema=1,complete=True,game_build='90447',policy='no-rain-v1',roundtrip_verified=True,
                source_prepared_manifest_sha256=hashlib.sha256((pack/'manifest.json').read_bytes()).hexdigest(),
                source_prepared_files=sources,output_files=files)
            (delta/'complete.json').write_text(json.dumps(receipt))
            self.assertEqual(verify_classic_weather(delta,pack),delta/'overlay')
            (delta/'overlay/data/00-00000008.data').write_bytes(b'changed')
            with self.assertRaises(ValueError):verify_classic_weather(delta,pack)
            (pack/'manifest.json').write_text('{}')
            with self.assertRaises(ValueError):verify_classic_weather(delta,pack)


if __name__=='__main__':unittest.main()
