# INPUT: Synthetic classic/HD descriptors and small RGBA images.
# OUTPUT: Numeric sharpening, original alpha/HD input and strict ABI refusal proofs.
# POS: Host tests; no game data, device session or private shader is required.
import math,random,struct,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from spirv_classic_display import sharpen
from optimize_shaders import interface

def rows(raw):
    w=struct.unpack('<%dI'%(len(raw)//4),raw);at=5
    while at<len(w):
        n=w[at]>>16;yield w[at:at+n];at+=n

def fixture():
    def op(code,*args):return [(len(args)+1)<<16|code,*args]
    string=struct.unpack('<4I',b'GLSL.std.450\0\0\0\0')
    r=[op(17,1),op(11,30,*string),op(71,8,34,1),op(71,8,33,1),op(71,9,34,1),op(71,9,33,2),
       op(22,1,32),op(23,2,1,2),op(23,3,1,4),op(21,4,32,1),op(25,5,1,1,0,0,0,1,0),
       op(27,6,5),op(32,7,0,6),op(59,7,8,0),op(59,7,9,0),
       op(43,1,10,0x3f000000),op(44,2,11,10,10),op(19,12),op(33,13,12),
       op(54,12,14,0,13),op(248,15),op(61,6,20,8),op(61,6,21,9),
       op(87,3,22,20,11,0),op(87,3,23,21,11,0),op(253),op(56)]
    w=[0x07230203,0x10300,0,64,0]+[x for a in r for x in a]
    meta=struct.pack('<12I',0x4d4b5644,1,1,2,0,0,0,1,1,1,1,0)
    meta+=struct.pack('<8i',1,1,9,4,12,0,1,0)+struct.pack('<8i',1,2,9,4,11,0,1,0)
    return struct.pack('<%dI'%len(w),*w),meta

def execute(raw,classic,hd):
    values={8:classic,9:hd}
    def each(a,b,fn):
        if isinstance(a,list):return [fn(x,y) for x,y in zip(a,b if isinstance(b,list) else [b]*len(a))]
        return fn(a,b)
    for a in rows(raw):
        op=a[0]&65535
        if op==43:values[a[2]]=struct.unpack('<f',struct.pack('<I',a[3]))[0] if a[1]==1 else a[3]
        elif op in (44,80):values[a[2]]=[values[i] for i in a[3:]]
        elif op==61:values[a[2]]=values[a[3]]
        elif op==87:
            uv=values[a[4]];x=max(0,min(4,int(uv[0]*5)));y=max(0,min(4,int(uv[1]*5)));values[a[2]]=values[a[3]][y][x][:]
        elif op==100:values[a[2]]=values[a[3]]
        elif op==103:values[a[2]]=[5,5]
        elif op==111:values[a[2]]=[float(x) for x in values[a[3]]]
        elif op==81:values[a[2]]=values[a[3]][a[4]]
        elif op in (129,131,136,142):
            f={129:lambda x,y:x+y,131:lambda x,y:x-y,136:lambda x,y:x/y,142:lambda x,y:x*y}[op]
            values[a[2]]=each(values[a[3]],values[a[4]],f)
        elif op==12:
            assert a[4]==43
            values[a[2]]=[min(hi,max(lo,x)) for x,lo,hi in zip(values[a[5]],values[a[6]],values[a[7]])]
        elif op==79:
            combined=values[a[3]]+values[a[4]];values[a[2]]=[combined[i] for i in a[5:]]
    return values[22],values[23]

class ClassicDisplayTests(unittest.TestCase):
    def test_numeric_local_filter_preserves_alpha_and_hd_sample(self):
        raw,meta=fixture()
        for amount in (.2,.4):
            out=sharpen(raw,meta,amount);rng=random.Random(83)
            for _ in range(100):
                classic=[[[rng.random() for _ in range(4)] for x in range(5)] for y in range(5)]
                hd=[[[rng.random() for _ in range(4)] for x in range(5)] for y in range(5)]
                c,h=execute(out,classic,hd);center=classic[2][2]
                for lane in range(3):
                    mean=sum(classic[y][x][lane] for x,y in [(1,2),(3,2),(2,1),(2,3)])/4
                    expected=min(1,max(0,center[lane]+amount*max(-.1,min(.1,center[lane]-mean))))
                    self.assertAlmostEqual(c[lane],expected,places=7)
                    self.assertLessEqual(abs(c[lane]-center[lane]),amount*.1+.000001)
                self.assertEqual(c[3],center[3]);self.assertEqual(h,hd[2][2])
            self.assertEqual(interface(raw)[:2],interface(out)[:2]);self.assertEqual(interface(out)[2]-interface(raw)[2],{50})
            self.assertEqual(sum(a[0]&65535==87 for a in rows(out)),6)
    def test_flat_and_linear_regions_do_not_gain_artificial_detail(self):
        raw,meta=fixture();out=sharpen(raw,meta)
        for image in ([[[.3,.4,.5,.123] for x in range(5)] for y in range(5)],
                      [[[.1+.05*x,.1+.05*y,.2,.987] for x in range(5)] for y in range(5)]):
            c,_=execute(out,image,image)
            for a,b in zip(c,image[2][2]):self.assertAlmostEqual(a,b,places=7)
    def test_bounds_and_unreviewed_layout_are_rejected(self):
        raw,meta=fixture()
        for amount in (0,-1,.6,math.nan,math.inf):
            with self.assertRaises(ValueError):sharpen(raw,meta,amount)
        for damaged in (meta[:4],meta[:-1],meta[:8]+struct.pack('<I',0)+meta[12:]):
            with self.assertRaises(ValueError):sharpen(raw,damaged)
        for n in (0,4,19,len(raw)-1):
            with self.assertRaises(ValueError):sharpen(raw[:n],meta)
if __name__=='__main__':unittest.main()
