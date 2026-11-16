# INPUT: Reviewed build-90447 classic compositor SPIR-V and unchanged metadata.
# OUTPUT: Mild RGB local sharpening before display, with original sample alpha.
# POS: Computer-only quality transform; callers validate and retain the original bank.
"""Explicit mild RGB unsharp mask on the reviewed classic compositor inputs.
Original UV, alpha, uniforms, blend, gamma and output ABI survive.
Only prepare_classic_display.py applies this to the verified build-90447 compositor.
"""
import math,struct

def sharpen(raw,metadata,amount=.4):
    if not math.isfinite(amount) or not 0<amount<=.5:raise ValueError('amount must be 0..0.5, excluding zero')
    if len(metadata)<48 or len(raw)%4:raise ValueError('truncated shader')
    w=list(struct.unpack('<%dI'%(len(raw)//4),raw))
    if len(w)<5 or w[0]!=0x07230203 or w[1]>0x10300:raise ValueError('SPIR-V 1.3 or earlier required')
    h=struct.unpack_from('<12I',metadata)
    if h[:3]!=(0x4d4b5644,1,1) or h[3]>64 or h[4]>32 or h[6]>32 or len(metadata)!=48+h[3]*32+h[4]*8+h[6]*4+h[5]:raise ValueError('fragment metadata required')
    bindings=[struct.unpack_from('<8i',metadata,48+i*32) for i in range(h[3])]
    if not all(sum(b[0]==1 and b[1]==i and b[3]==4 for b in bindings)==1 for i in (1,2)):raise ValueError('classic input bindings required')
    inst=[];at=5
    while at<len(w):
        n=w[at]>>16
        if not n or at+n>len(w):raise ValueError('instruction bounds')
        inst.append(w[at:at+n]);at+=n
    decor={};types={};defs={};glsl=0
    for a in inst:
        op=a[0]&65535
        if op==71 and len(a)==4:decor[(a[1],a[2])]=a[3]
        if 19<=op<=33:types[a[1]]=a
        if op in (12,59,61,65,80,81,169):defs[a[2]]=a
        if op==11 and struct.pack('<%dI'%(len(a)-2),*a[2:]).rstrip(b'\0')==b'GLSL.std.450':glsl=a[1]
    samples=[]
    for i,a in enumerate(inst):
        if a[0]&65535!=87:continue
        load=defs.get(a[3]);var=load[3] if load and load[0]&65535==61 else 0
        if decor.get((var,34))==1 and decor.get((var,33)) in (1,2):
            if len(a)!=6 or a[5]!=0 or not load:raise ValueError('ordinary implicit sample required')
            samples.append((i,a,types[load[1]][2]))
    if len(samples)!=2 or {decor[(defs[a[3]][3],33)] for _,a,_ in samples}!={1,2} or not glsl:raise ValueError('two classic samples and GLSL import required')
    result_type=samples[0][1][1];v4=types[result_type];scalar=v4[2]
    if v4[0]&65535!=23 or v4[3]!=4 or types[scalar][0]&65535!=22 or types[scalar][1:]!=[scalar,32] or any(a[1]!=result_type for _,a,_ in samples):raise ValueError('float4 color required')
    v2=next(k for k,a in types.items() if a[0]&65535==23 and a[2:]==[scalar,2])
    sint=next(k for k,a in types.items() if a[0]&65535==21 and a[2:]==[32,1])
    bound=w[3]
    def alloc():
        nonlocal bound
        i=bound;bound+=1;return i
    def op(code,*args):return [(len(args)+1)<<16|code,*args]
    def result(code,typ,*args):
        rid=alloc();return rid,op(code,typ,rid,*args)
    declarations=[]
    ivec2=next((k for k,a in types.items() if a[0]&65535==23 and a[2:]==[sint,2]),None)
    if ivec2 is None:ivec2=alloc();declarations.append(op(23,ivec2,sint,2))
    constants={}
    for value in (0.,1.,.25,float(amount),-.1,.1):
        rid=alloc();declarations.append(op(43,scalar,rid,struct.unpack('<I',struct.pack('<f',value))[0]));constants[value]=rid
    zeroi=alloc();declarations.append(op(43,sint,zeroi,0))
    vectors={}
    for value in (-.1,.1,0.,1.):
        rid=alloc();declarations.append(op(44,result_type,rid,*([constants[value]]*4)));vectors[value]=rid
    ones=alloc();declarations.append(op(44,v2,ones,constants[1.],constants[1.]))
    replacements={}
    for i,a,image_type in samples:
        # Binding 1 is the original 800x600 classic scene. Binding 2 is the
        # HD/composition input and remains entirely untouched.
        if decor[(defs[a[3]][3],33)]!=1:continue
        code=[]
        def emit(opcode,typ,*args):
            rid,row=result(opcode,typ,*args);code.append(row);return rid
        center=alloc();row=a.copy();row[2]=center;code.append(row)
        image=emit(100,image_type,a[3]);size=emit(103,ivec2,image,zeroi);dimensions=emit(111,v2,size)
        reciprocal=emit(136,v2,ones,dimensions);dx=emit(81,scalar,reciprocal,0);dy=emit(81,scalar,reciprocal,1)
        ox=emit(80,v2,dx,constants[0.]);oy=emit(80,v2,constants[0.],dy)
        neighbours=[]
        for axis in (ox,oy):
            for sign in (129,131):
                uv=emit(sign,v2,a[4],axis);sample=alloc();q=a.copy();q[2]=sample;q[4]=uv;code.append(q);neighbours.append(sample)
        s0=emit(129,result_type,neighbours[0],neighbours[1]);s1=emit(129,result_type,neighbours[2],neighbours[3]);total=emit(129,result_type,s0,s1)
        avg=emit(142,result_type,total,constants[.25]);detail=emit(131,result_type,center,avg)
        limited=emit(12,result_type,glsl,43,detail,vectors[-.1],vectors[.1]);scaled=emit(142,result_type,limited,constants[float(amount)])
        rgb=emit(129,result_type,center,scaled);clamped=emit(12,result_type,glsl,43,rgb,vectors[0.],vectors[1.])
        code.append(op(79,result_type,a[2],clamped,center,0,1,2,7))
        replacements[i]=code
    out=[];added=False
    has_query=any(a==op(17,50) for a in inst)
    for i,a in enumerate(inst):
        if a[0]&65535!=17 and not added:
            if not has_query:out.append(op(17,50))
            added=True
        if a[0]&65535==54 and declarations:out.extend(declarations);declarations=[]
        out.extend(replacements.get(i,[a]))
    flat=[*w[:3],bound,w[4],*(x for a in out for x in a)]
    return struct.pack('<%dI'%len(flat),*flat)

