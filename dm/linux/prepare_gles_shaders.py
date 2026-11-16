#!/usr/bin/env python3
# INPUT: Verified Vulkan shader bank and an explicit local SPIRV-Cross executable.
# OUTPUT: Separate GLSL ES 3.20 experiment bank, binding metadata and failure ledger.
# POS: Computer-only preparation; never changes the input bank or guest resources.
"""Prepare the optional GLES renderer's shaders; unsupported programs stay explicit."""
import argparse
import concurrent.futures
import gzip
import hashlib
import json
import re
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def draw_uniforms(words):
    """Replace unsupported draw builtins by uniforms set from each actual draw."""
    ops, at = [], 5
    while at < len(words):
        n = words[at] >> 16
        if not n or at+n > len(words):
            raise ValueError('truncated SPIR-V instruction')
        ops.append(words[at:at+n])
        at += n
    fields = {a[1]: a[3]-4424 for a in ops if a[0] & 65535 == 71 and len(a) == 4 and a[2] == 11 and 4424 <= a[3] <= 4426}
    if not fields:
        return words
    variables = {a[2]: a for a in ops if a[0] & 65535 == 59 and a[2] in fields}
    if len(variables) != len(fields) or any(len(a) != 4 or a[3] != 1 for a in variables.values()):
        raise ValueError('unsupported draw parameter storage')
    pointers = {a[1]: a for a in ops if a[0] & 65535 == 32}
    types = {}
    for a in variables.values():
        old = pointers.get(a[1])
        if old is None or old[2] != 1:
            raise ValueError('unsupported draw parameter pointer')
        if old[1] not in types:
            types[old[1]] = words[3]+len(types)
    result = words[:5]
    result[3] += len(types)
    names_added = False
    for a in ops:
        op = a[0] & 65535
        if not names_added and 19 <= op <= 39:
            for var, field in fields.items():
                text = ('dm_BaseVertex', 'dm_BaseInstance', 'dm_DrawID')[field].encode()+b'\0'
                text += b'\0'*(-len(text) % 4)
                name = list(struct.unpack('<%dI' % (len(text)//4), text))
                result.extend([(2+len(name)) << 16 | 5, var, *name])
            names_added = True
        if op == 17 and a[1] == 4427:
            continue
        if op in (5, 71) and a[1] in fields:
            continue
        if op == 15:
            raw = struct.pack('<%dI' % (len(a)-3), *a[3:])
            end = 3 + raw.index(0)//4 + 1
            a = a[:end]+[x for x in a[end:] if x not in fields]
            a[0] = len(a) << 16 | 15
        if op == 59 and a[2] in fields:
            a = [a[0], types[a[1]], a[2], 0]
        result.extend(a)
        if op == 32 and a[1] in types:
            result.extend([4 << 16 | 32, types[a[1]], 0, a[3]])
    return result


def storage_image_formats(bindings, words):
    """Keep unknown-format image work; specialize its qualifier at first bind."""
    scalars, pointers, variables, units, images = {}, {}, {}, {}, {}
    at = 5
    while at < len(words):
        n, op = words[at] >> 16, words[at] & 65535
        if op == 22 and words[at+2] == 32:
            scalars[words[at+1]] = 0
        elif op == 21 and words[at+2] == 32:
            scalars[words[at+1]] = 1 if words[at+3] else 2
        elif op == 25 and n >= 9 and words[at+7] == 2 and words[at+8] == 0:
            images[words[at+1]] = at
        elif op == 32:
            pointers[words[at+1]] = words[at+3]
        elif op == 59:
            variables[words[at+2]] = words[at+1]
        elif op == 71 and n == 4 and words[at+2] == 33:
            units[words[at+1]] = words[at+3]
        at += n
    used = set()
    for variable, pointer in variables.items():
        image = pointers.get(pointer)
        if image not in images:
            continue
        at = images[image]
        scalar = scalars.get(words[at+2])
        matches = [b for b in bindings if b[0] == units.get(variable) and b[2] == 3 and b[3] == 6]
        if scalar is None or len(matches) != 1:
            raise ValueError('unformatted storage image requires binding metadata and 32-bit scalar')
        matches[0][3] = 15
        matches[0][7] = scalar | (words[at+3] << 4) | (words[at+5] << 8) | (words[at+6] << 9)
        # A compiler placeholder, replaced by the bound NVN format before GLSL compilation.
        words[at+8] = (1, 21, 30)[scalar]
        used.add(image)
    if used != set(images):
        raise ValueError('unformatted storage image requires binding metadata')
    return words


def remap(meta, module, texture_budget=16):
    if len(meta) < 48:
        raise ValueError('truncated source metadata')
    h = list(struct.unpack_from('<12I', meta))
    if h[0:2] != [0x4d4b5644, 1] or h[2] not in (0, 1, 5):
        raise ValueError('invalid source metadata')
    if len(meta) != 48 + h[3]*32 + h[4]*8 + h[6]*4 + h[5]:
        raise ValueError('invalid source metadata length')
    if h[11] and h[11] != len(module):
        raise ValueError('source SPIR-V length differs from metadata')
    bindings = [list(struct.unpack_from('<8i', meta, 48+i*32)) for i in range(h[3])]
    counts = [0]*4
    mapping = {}
    for b in bindings:
        if b[6] != 1:
            raise ValueError('descriptor arrays require a separate GLES adaptation')
        category = 0 if b[2] == 6 else 1 if b[2] == 7 else 2 if b[2] in (1, 2, 4) else 3
        limit = (32, 16, texture_budget, 8)[category]
        if counts[category] >= limit:
            raise ValueError('stage binding budget exceeded')
        unit = counts[category] + ((32, 16, 16, 8)[category] if h[2] == 1 else 0)
        counts[category] += 1
        if b[1] in mapping:
            raise ValueError('duplicate original binding')
        mapping[b[1]] = unit
        b[0] = unit
    if len(module) % 4 or len(module) < 20:
        raise ValueError('invalid SPIR-V length')
    w = list(struct.unpack('<%dI' % (len(module)//4), module))
    if len(w) < 5 or w[0] != 0x07230203:
        raise ValueError('invalid SPIR-V')
    cursor = 5
    while cursor < len(w):
        n, op = w[cursor] >> 16, w[cursor] & 65535
        if not n or cursor+n > len(w):
            raise ValueError('truncated SPIR-V instruction')
        if op == 71 and n == 4 and w[cursor+2] == 33:
            w[cursor+3] = mapping[w[cursor+3]]
        elif op == 71 and n == 4 and w[cursor+2] == 34:
            w[cursor+3] = 0
        cursor += n
    w = draw_uniforms(storage_image_formats(bindings, w))
    h[0], h[1], h[11] = 0x4d4c4744, 7, 0
    packed = struct.pack('<12I', *h) + b''.join(struct.pack('<8i', *b) for b in bindings) + meta[48+len(bindings)*32:]
    return packed, struct.pack('<%dI' % len(w), *w)


def vertex_window_modes(source):
    start = source.find('void main()')
    opening = source.find('{', start)
    if start < 0 or opening < 0:
        raise ValueError('missing vertex main')
    depth, closing = 1, opening+1
    while closing < len(source) and depth:
        depth += (source[closing] == '{') - (source[closing] == '}')
        closing += 1
    if depth:
        raise ValueError('unterminated vertex main')
    # SPIRV-Cross applies zero-to-one -> minus-one-to-one depth conversion.
    # Undo it only for a guest that already uses minus-one-to-one depth.
    tail = '\n    gl_Position.y *= dm_OriginY;\n    if (dm_DepthZero == 0) gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\n'
    uniforms = 'uniform highp float dm_OriginY;\nuniform highp int dm_DepthZero;\n\n'
    return source[:start]+uniforms+source[start:closing-1]+tail+source[closing-1:]


def vertex_storage_textures(meta, source):
    """Lower read-only uint storage loads for Mali's zero vertex-SSBO limit."""
    h = struct.unpack_from('<12I', meta)
    if h[2] != 0:
        return meta, source
    bindings = [list(b) for b in struct.iter_unpack('<8i', meta[48:48+h[3]*32])]
    unit = sum(b[2] in (1, 2, 4) for b in bindings)
    for b in bindings:
        if b[2] != 7:
            continue
        if b[7] or b[3] not in (3, 9):
            raise ValueError('vertex storage writes cannot use read-only texture buffers')
        if unit >= 16:
            raise ValueError('vertex storage texture binding budget exceeded')
        declaration = re.compile(r'layout\(binding = '+str(b[0])+r', std430\) buffer \w+\s*\{\s*uint (\w+)\[\];\s*\}\s*(\w+);')
        matches = list(declaration.finditer(source))
        if len(matches) != 1:
            raise ValueError('vertex storage requires one uint array block')
        match = matches[0]
        member, instance = match.groups()
        sampler = 'dm_StorageTexture_'+str(unit)
        load = 'dm_StorageLoad_'+str(unit)
        replacement = ('layout(binding = '+str(unit)+') uniform highp usampler2D '+sampler+';\n'
                       'uint '+load+'(int offset) { return texelFetch('+sampler+', ivec2(offset & 2047, offset >> 11), 0).r; }')
        source = source[:match.start()]+replacement+source[match.end():]
        access = re.compile(r'\b'+re.escape(instance)+r'\.'+re.escape(member)+r'\[')
        while True:
            match = access.search(source)
            if match is None:
                break
            at, depth = match.end(), 1
            end = at
            while end < len(source) and depth:
                depth += (source[end] == '[') - (source[end] == ']')
                end += 1
            if depth or re.match(r'\s*(?:[+\-*/%&|^]?=(?!=)|\+\+|--)', source[end:]):
                raise ValueError('vertex storage access is not a supported read')
            source = source[:match.start()]+load+'(int('+source[at:end-1]+'))'+source[end:]
        if re.search(r'\b'+re.escape(instance)+r'\b', source):
            raise ValueError('untranslated vertex storage use')
        b[0], b[2], b[3] = unit, 4, 12 if b[3] == 3 else 13
        unit += 1
    packed = meta[:48]+b''.join(struct.pack('<8i', *b) for b in bindings)+meta[48+h[3]*32:]
    return packed, source


def buffer_storage_source(unit, binding, prefix):
    """Decode a typed read-only buffer without a sampler or texture copy."""
    vector = prefix+'vec4' if prefix else 'vec4'
    convert = 'int(raw)' if prefix == 'i' else 'raw' if prefix == 'u' else '(info.w == 0 ? float(raw) / 255.0 : info.w == 2 ? unpackHalf2x16(raw).x : uintBitsToFloat(raw))'
    return f'''layout(std430, binding = {binding}) readonly buffer dm_BufferStorage_{unit} {{ highp uint dm_BufferData_{unit}[]; }};
uniform highp ivec4 dm_BufferLayout_{unit};
uniform highp int dm_BufferTexels_{unit};
highp {vector} dm_BufferFetch_{unit}(int index) {{
    if (index < 0 || index >= dm_BufferTexels_{unit}) return {vector}(0);
    ivec4 info = dm_BufferLayout_{unit};
    highp {vector} value = {vector}(0, 0, 0, 1);
    int bits = info.w < 2 ? 8 : info.w == 2 ? 16 : 32;
    for (int component = 0; component < 4; component++) {{
        if (component >= info.z) break;
        int byteOffset = info.x + index * info.y + component * (bits / 8);
        int shift = (byteOffset & 3) * 8;
        uint raw = dm_BufferData_{unit}[byteOffset >> 2] >> uint(shift);
        if (shift + bits > 32) raw |= dm_BufferData_{unit}[(byteOffset >> 2) + 1] << uint(32 - shift);
        if (bits < 32) raw &= (1u << uint(bits)) - 1u;
        value[component] = {convert};
    }}
    return value;
}}'''


def sampled_buffer_textures(meta, source, storage=False):
    """Read buffer samplers through 2D snapshots without dropping their work."""
    h = struct.unpack_from('<12I', meta)
    bindings = [list(b) for b in struct.iter_unpack('<8i', meta[48:48+h[3]*32])]
    declaration = re.compile(r'layout\(binding = (\d+)\) uniform (?:(?:highp|mediump|lowp) )?([iu]?)samplerBuffer (\w+);')
    while True:
        match = declaration.search(source)
        if match is None:
            break
        unit, prefix, name = int(match[1]), match[2], match[3]
        binding = [b for b in bindings if b[0] == unit and b[2] in (1, 2, 4)]
        if len(binding) != 1 or binding[0][3] not in (4, 10, 11):
            raise ValueError('buffer sampler metadata does not match declaration')
        fetch, size = 'dm_BufferFetch_'+str(unit), 'dm_BufferTexels_'+str(unit)
        marker = '/* dm_buffer_declaration */'
        source = source[:match.start()]+marker+source[match.end():]
        call = re.compile(r'\b(texelFetch|textureSize)\s*\(\s*'+re.escape(name)+r'\s*')
        while True:
            use = call.search(source)
            if use is None:
                break
            at = use.end()
            if use[1] == 'textureSize':
                if source[at:at+1] != ')':
                    raise ValueError('unexpected buffer textureSize arguments')
                source = source[:use.start()]+size+source[at+1:]
                continue
            if source[at:at+1] != ',':
                raise ValueError('missing buffer texelFetch index')
            begin, end, depth = at+1, at+1, 1
            while end < len(source) and depth:
                ch = source[end]
                if ch == ',' and depth == 1:
                    raise ValueError('unexpected buffer texelFetch arguments')
                depth += (ch == '(') - (ch == ')')
                end += 1
            if depth:
                raise ValueError('unterminated buffer texelFetch')
            source = source[:use.start()]+fetch+'('+source[begin:end-1].strip()+')'+source[end:]
        if re.search(r'\b'+re.escape(name)+r'\b', source):
            raise ValueError('untranslated buffer sampler use')
        vector = prefix+'vec4' if prefix else 'vec4'
        use_storage = storage and h[2] in (1, 5)
        if use_storage:
            base = 16 if h[2] == 1 else 0
            used = {b[0] for b in bindings if b[2] == 7}
            target = next((x for x in range(base, base+16) if x not in used), None)
            if target is None:
                raise ValueError('buffer sampler storage binding budget exceeded')
            replacement = buffer_storage_source(unit, target, prefix)
            binding[0][0], binding[0][1], binding[0][2], binding[0][3] = target, unit, 7, 16
        else:
            replacement = ('layout(binding = '+str(unit)+') uniform highp '+prefix+'sampler2D '+name+';\n'
                           'uniform highp int '+size+';\n'
                           'highp '+vector+' '+fetch+'(int index) { return texelFetch('+name+', ivec2(index & 2047, index >> 11), 0); }')
            binding[0][3] = 14
        source = source.replace(marker, replacement, 1)
        binding[0][7] = {'': 0, 'i': 1, 'u': 2}[prefix]
    if not re.search(r'\b[iu]?imageBuffer\b', source):
        source = re.sub(r'^#extension GL_(?:EXT|OES)_texture_buffer\s*:\s*require\s*\n', '', source, flags=re.M)
    header = list(h)
    if storage:
        header[1] = 8
    packed = struct.pack('<12I', *header)+b''.join(struct.pack('<8i', *b) for b in bindings)+meta[48+h[3]*32:]
    return packed, source


def fragment_quad_shuffle(source):
    """Explicit low-quality fragment derivative fallback, never compute sync."""
    if 'subgroupShuffle(' not in source or re.search(r'\bsubgroup(?!Shuffle\b)\w+|\bgl_Subgroup(?!InvocationID\b)\w+', source):
        raise ValueError('fragment subgroup operation has no quad approximation')
    source = re.sub(r'^#extension GL_KHR_shader_subgroup_(?:basic|shuffle)\s*:\s*require\s*\n', '', source, flags=re.M)
    source = source.replace('gl_SubgroupInvocationID', 'dm_QuadLane()').replace('subgroupShuffle(', 'dm_QuadShuffle(')
    source = re.sub(r'layout\(set = 0, binding = ', 'layout(binding = ', source)
    helper = '''uint dm_QuadLane() { return (uint(gl_FragCoord.x) & 1u) | ((uint(gl_FragCoord.y) & 1u) << 1u); }
float dm_QuadShuffle(float value, uint lane) {
    uint current = dm_QuadLane(), flip = lane ^ current;
    float dx = dFdx(value), dy = dFdy(value);
    float horizontal = value + ((flip & 1u) != 0u ? ((current & 1u) != 0u ? -dx : dx) : 0.0);
    float vertical = dFdy(horizontal);
    return horizontal + ((flip & 2u) != 0u ? ((current & 2u) != 0u ? -vertical : vertical) : 0.0);
}
'''
    start = re.search(r'^(?:int|float|void) \w+\([^;]*\)\s*\{', source, re.M)
    if not start:
        raise ValueError('fragment function insertion point missing')
    return source[:start.start()]+helper+source[start.start():]


def reduced_sampler_budget(meta, source, limit=16):
    """Trade shadow/volume quality for bounded fragment sampler use, explicitly."""
    h = list(struct.unpack_from('<12I', meta))
    bindings = [list(b) for b in struct.iter_unpack('<8i', meta[48:48+h[3]*32])]
    textures = [b for b in bindings if b[2] in (1, 2, 4)]
    if len(textures) <= limit:
        return meta, source, []
    if h[2] != 1:
        raise ValueError('texture budget reduction is fragment-only')
    removed = []
    for shadow in (True, False):
        for b in textures:
            if len(textures)-len(removed) <= limit:
                break
            declaration = re.compile(r'layout\(binding = '+str(b[0])+r'\) uniform (?:(?:highp|mediump|lowp) )?((?:[iu]?sampler)\w+) (\w+);')
            match = declaration.search(source)
            if not match:
                continue
            sampler, name = match.groups()
            if shadow != sampler.endswith('Shadow') or (not shadow and sampler != 'sampler3D'):
                continue
            coordinate = 'vec4' if sampler in ('samplerCubeShadow', 'sampler2DArrayShadow') else 'vec3'
            derivative = 'vec2' if sampler == 'sampler2DArrayShadow' else 'vec3'
            result = 'float' if shadow else 'vec4'
            value = '1.0' if shadow else 'vec4(clamp(coordinate, vec3(0.0), vec3(1.0)), 1.0)'
            sample = 'dm_ReducedSample_'+str(b[0]);size = 'dm_ReducedSize_'+str(b[0])
            dimension = 'ivec3' if sampler in ('sampler3D', 'sampler2DArrayShadow') else 'ivec2'
            helper = (f'{result} {sample}({coordinate} coordinate) {{ return {value}; }}\n'
                      f'{result} {sample}({coordinate} coordinate, float level) {{ return {value}; }}\n'
                      f'{result} {sample}({coordinate} coordinate, {derivative} dx, {derivative} dy) {{ return {value}; }}\n'
                      f'{dimension} {size}(int level) {{ return {dimension}(1); }}')
            source = source[:match.start()]+helper+source[match.end():]
            source = re.sub(r'\b(?:texture|textureLod|textureGrad)\s*\(\s*'+re.escape(name)+r'\s*,', sample+'(', source)
            source = re.sub(r'\btextureSize\s*\(\s*'+re.escape(name)+r'\s*,', size+'(', source)
            if re.search(r'\b'+re.escape(name)+r'\b', source):
                raise ValueError('unsupported reduced sampler use')
            removed.append(b)
    if len(textures)-len(removed) > limit:
        raise ValueError('fragment texture budget still exceeds hardware')
    kept = [b for b in bindings if b not in removed];h[3] = len(kept);h[1] = 8;h[11] |= 1 << 15
    return struct.pack('<12I', *h)+b''.join(struct.pack('<8i', *b) for b in kept)+meta[48+len(bindings)*32:], source, [b[4] for b in removed]



def compute_workgroup_shuffle(meta, source):
    """Lower a reconverged 32-lane shuffle to shared memory and real barriers.

    Accept only one wrapper, called directly by every invocation in main.
    Divergent subgroup ballots and conditional/looped wrapper calls remain
    unsupported; inserting workgroup barriers there could deadlock the GPU.
    """
    h = struct.unpack_from('<12I', meta)
    if h[2] != 5 or tuple(h[7:10]) != (32, 1, 1):
        raise ValueError('compute shuffle requires one complete 32-lane group')
    if re.search(r'\bsubgroup(?!Shuffle\b)\w+|\bgl_Subgroup(?!InvocationID\b)\w+', source):
        raise ValueError('compute subgroup operation is not a reconverged shuffle')
    expression = re.search(r'return (\w+) \? floatBitsToInt\(subgroupShuffle\(intBitsToFloat\((\w+)\), uint\((\w+)\)\)\) : (\w+);', source)
    if expression is None or expression[2] != expression[4] or source.count('subgroupShuffle(') != 1:
        raise ValueError('compute shuffle wrapper shape is unsupported')
    start = source.rfind('\nint ', 0, expression.start())
    wrapper = re.match(r'\nint (\w+)\([^{}]*\)\s*\{', source[start:]) if start >= 0 else None
    main = source.find('void main()')
    if wrapper is None or main < expression.end():
        raise ValueError('compute shuffle wrapper cannot be identified')
    name = wrapper[1]
    wrapper_body = source[start+wrapper.end():expression.start()]
    if re.search(r'[{}]|\b(?:if|for|while|switch|return)\b', wrapper_body):
        raise ValueError('compute shuffle wrapper is divergent')
    body = source[main:]
    depth = 0;calls = []
    for match in re.finditer(r'[{}]|\b'+re.escape(name)+r'\s*\(|\breturn\b', body):
        token = match[0]
        if token == '{':depth += 1
        elif token == '}':depth -= 1
        elif token == 'return':
            # Even a conditional early return could remove a participant.
            calls.append(('return', match.start()))
        else:
            if depth != 1:raise ValueError('compute shuffle call is divergent')
            statement = max(body.rfind(';', 0, match.start()), body.rfind('{', 0, match.start()), body.rfind('}', 0, match.start()))
            prefix = body[statement+1:match.start()].strip()
            if not re.fullmatch(r'(?:int\s+)?\w+\s*=\s*', prefix):
                raise ValueError('compute shuffle call is conditionally evaluated')
            calls.append(('call', match.start()))
    last = max((position for kind,position in calls if kind == 'call'), default=-1)
    if last < 0 or any(kind == 'return' and position < last for kind,position in calls):
        raise ValueError('compute shuffle participants can exit early')
    if len(re.findall(r'\b'+re.escape(name)+r'\s*\(', source)) != 1+sum(kind == 'call' for kind,position in calls):
        raise ValueError('compute shuffle wrapper has another caller')
    replacement = ('int dm_ShuffledValue = int(dm_WorkgroupShuffle(uint('+expression[2]+'), uint('+expression[3]+')));\n'
                   '    return '+expression[1]+' ? dm_ShuffledValue : '+expression[4]+';')
    source = source[:expression.start()]+replacement+source[expression.end():]
    source = re.sub(r'^#extension GL_KHR_shader_subgroup_(?:basic|shuffle)\s*:\s*require\s*\n', '', source, flags=re.M)
    source = re.sub(r'layout\(set = 0, binding = ', 'layout(binding = ', source)
    source = source.replace('gl_SubgroupInvocationID', 'gl_LocalInvocationIndex')
    helper = ('shared uint dm_ShuffleWords[32];\n'
              'uint dm_WorkgroupShuffle(uint value, uint lane) {\n'
              '    dm_ShuffleWords[gl_LocalInvocationIndex] = value;\n'
              '    memoryBarrierShared(); barrier();\n'
              '    uint result = dm_ShuffleWords[lane & 31u];\n'
              '    memoryBarrierShared(); barrier();\n'
              '    return result;\n'
              '}\n')
    start = source.index('\nint '+name+'(')
    return source[:start]+'\n'+helper+source[start:]


def independent_compute_groups(meta, source):
    """Split independent invocations to the ES 3.1 minimum group limits."""
    h = list(struct.unpack_from('<12I', meta))
    if h[2] != 5:
        return meta, source
    original = list(h[7:10])
    native, shifts = original[:], [0, 0, 0]
    if any(n < 1 for n in native):
        raise ValueError('invalid compute workgroup dimensions')
    def oversized():
        return native[0]*native[1]*native[2] > 128 or any(n > cap for n, cap in zip(native, (128, 128, 64)))
    if not oversized():
        return meta, source
    if re.search(r'\b(?:shared|subgroup\w*|gl_Subgroup\w*)\b|\b(?:barrier|\w*Barrier\w*|atomic\w*|imageAtomic\w*)\s*\(', source):
        return meta, source
    while oversized():
        candidates = [i for i in (2, 1, 0) if native[i] > 1 and native[i] % 2 == 0]
        if not candidates:
            raise ValueError('compute group cannot be split exactly')
        over_axis = [i for i in candidates if native[i] > (128, 128, 64)[i]]
        axis = (over_axis or candidates)[0]
        native[axis] //= 2
        shifts[axis] += 1
    scale = [1 << n for n in shifts]
    vector = lambda values: 'uvec3('+', '.join(str(n)+'u' for n in values)+')'
    logical_size, native_size, factors = map(vector, (original, native, scale))
    names = {
        'gl_WorkGroupID': 'dm_SplitGroup()',
        'gl_LocalInvocationID': 'dm_SplitLocal()',
        'gl_GlobalInvocationID': '(dm_SplitGroup() * '+logical_size+' + dm_SplitLocal())',
        'gl_NumWorkGroups': '(gl_NumWorkGroups / '+factors+')',
        'gl_WorkGroupSize': logical_size,
        'gl_LocalInvocationIndex': '(dm_SplitLocal().x + '+str(original[0])+'u * (dm_SplitLocal().y + '+str(original[1])+'u * dm_SplitLocal().z))',
    }
    start = source.find('void main()')
    if start < 0 or any(re.search(r'\b'+name+r'\b', source[:start]) for name in names if name != 'gl_WorkGroupSize'):
        raise ValueError('compute invocation use precedes entry point')
    source = re.sub(r'\b(?:'+('|'.join(names))+r')\b', lambda match: names[match[0]], source)
    before = 'layout(local_size_x = %d, local_size_y = %d, local_size_z = %d) in;' % tuple(original)
    after = 'layout(local_size_x = %d, local_size_y = %d, local_size_z = %d) in;' % tuple(native)
    if before not in source:
        raise ValueError('compute group declaration differs from metadata')
    source = source.replace(before, after, 1)
    helpers = ('uvec3 dm_SplitGroup() { return gl_WorkGroupID / '+factors+'; }\n'
               'uvec3 dm_SplitLocal() { return gl_LocalInvocationID + (gl_WorkGroupID % '+factors+') * '+native_size+'; }\n\n')
    start = source.index('void main()')
    source = source[:start]+helpers+source[start:]
    if any(n > 31 for n in shifts):
        raise ValueError('compute dispatch multiplier overflow')
    h[11] = (h[11] & ~32767) | shifts[0] | shifts[1] << 5 | shifts[2] << 10
    return struct.pack('<12I', *h)+meta[48:], source


def fragment_full_precision(meta, source):
    """NVN registers carry 32-bit values, including float/integer bit payloads."""
    if struct.unpack_from('<I', meta, 8)[0] != 1:
        return source
    return re.sub(r'\b(?:lowp|mediump)\b', 'highp', source)


def graphics_interfaces(meta, source):
    """Give a graphics pair matching varyings and independent resource names."""
    stage = struct.unpack_from('<I', meta, 8)[0]
    if stage not in (0, 1):
        return source
    prefix = 'dm_vs_' if stage == 0 else 'dm_fs_'
    names = {}
    blocks = re.compile(r'layout\([^)]*\)\s+(?:uniform|buffer)\s+(\w+)\s*\{[^}]*\}\s*(\w+)\s*;', re.S)
    for block in blocks.finditer(source):
        for name in block.groups():
            names[name] = prefix+name
    opaque = re.compile(r'layout\([^)]*\)\s+uniform\s+(?:(?:lowp|mediump|highp)\s+)?(?:[iu]?sampler\w+|[iu]?image\w+)\s+(\w+)\s*;')
    for uniform in opaque.finditer(source):
        names[uniform[1]] = prefix+uniform[1]
    direction = 'out' if stage == 0 else 'in'
    for line in source.splitlines():
        location = re.match(r'layout\(location = (\d+)\)', line)
        if location and re.search(r'\b'+direction+r'\b', line):
            name = re.search(r'(\w+)\s*(?:\[[^]]*\])?\s*;$', line)
            if not name:
                raise ValueError('graphics varying declaration cannot be named')
            names[name[1]] = 'dm_Varying_'+location[1]
    return re.sub(r'\b\w+\b', lambda match: names.get(match[0], match[0]), source)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--spirv-cross', type=Path, required=True)
    p.add_argument('--jobs', type=int, default=2)
    p.add_argument('--filter-unsupported', action='store_true')
    p.add_argument('--buffer-storage', action='store_true')
    p.add_argument('--low-texture-budget', action='store_true')
    a = p.parse_args()
    source, out, cross = a.source.resolve(), a.out.resolve(), a.spirv_cross.resolve()
    if out.exists() or source == out or source in out.parents or not 1 <= a.jobs <= 8:
        p.error('use a fresh separate output directory and jobs 1..8')
    if not (source/'shader_index.bin').is_file() or not cross.is_file():
        p.error('full-code index and explicit compiler executable required')
    metadata=sorted(source.glob('*.vkmeta'))
    if not metadata:p.error('source bank contains no shader metadata')
    out.mkdir(parents=True)
    shutil.copyfile(source/'shader_index.bin', out/'shader_index.bin')

    def translate(meta_path):
        key = meta_path.stem
        # Preserve original NVN reads/writes even when GLES translation is unsupported.
        (out/(key+'.glfacts')).write_bytes(meta_path.read_bytes())
        try:
            raw = (source/(key+'.spv')).read_bytes() if (source/(key+'.spv')).exists() else gzip.decompress((source/(key+'.spv.gz')).read_bytes())
            packed, module = remap(meta_path.read_bytes(), raw, texture_budget=32 if a.buffer_storage else 16)
            quad_approximation = False
            with tempfile.TemporaryDirectory(prefix='gles-', dir=out) as work:
                spv = Path(work)/'shader.spv'
                spv.write_bytes(module)
                r = subprocess.run([str(cross), str(spv), '--es', '--version', '320', '--fixup-clipspace', '--no-support-nonzero-baseinstance'],
                                   text=True, capture_output=True, timeout=45)
                if r.returncode and struct.unpack_from('<I', packed, 8)[0] == 5 and 'subgroup operation' in r.stderr:
                    r = subprocess.run([str(cross), str(spv), '--es', '--version', '320', '--vulkan-semantics'],text=True,capture_output=True,timeout=45)
                    if not r.returncode:r.stdout = compute_workgroup_shuffle(packed, r.stdout)
                if r.returncode and a.low_texture_budget and struct.unpack_from('<I',packed,8)[0] == 1 and 'subgroup operation' in r.stderr:
                    nb = struct.unpack_from('<I', packed, 12)[0]
                    bindings = list(struct.iter_unpack('<8i', packed[48:48+nb*32]))
                    if any(b[2] not in (1, 2, 4, 6, 7) or (b[2] == 7 and b[7]) for b in bindings):
                        raise ValueError('fragment quad approximation requires read-only resources')
                    r = subprocess.run([str(cross), str(spv), '--es', '--version', '320', '--vulkan-semantics'],text=True,capture_output=True,timeout=45)
                    if not r.returncode:
                        r.stdout = fragment_quad_shuffle(r.stdout);quad_approximation = True
            if r.returncode:
                raise ValueError(r.stderr.strip()[:2000])
            if not r.stdout.startswith('#version 320 es'):
                raise ValueError('compiler did not emit GLSL ES 3.20')
            text = vertex_window_modes(r.stdout) if struct.unpack_from('<I',packed,8)[0] == 0 else r.stdout
            packed, text = vertex_storage_textures(packed, text)
            packed, text = sampled_buffer_textures(packed, text, storage=a.buffer_storage)
            removed = []
            if a.low_texture_budget:
                packed, text, removed = reduced_sampler_budget(packed, text)
            if quad_approximation:
                header = list(struct.unpack_from('<12I', packed));header[1] = 8;header[11] |= 1 << 15;packed = struct.pack('<12I', *header)+packed[48:]
            if not re.search(r'\buniform\s+(?:(?:highp|mediump|lowp)\s+)?[iu]?texture\w+\b|\b(?:textureSize|textureQueryLod|textureQueryLevels|textureSamples)\s*\(', text):
                text = re.sub(r'^#extension GL_EXT_samplerless_texture_functions\s*:\s*require\s*\n', '', text, flags=re.M)
            packed, text = independent_compute_groups(packed, text)
            text = fragment_full_precision(packed, text)
            text = graphics_interfaces(packed, text)
            (out/(key+'.glsl')).write_text(text)
            (out/(key+'.glmeta')).write_bytes(packed)
            nb = struct.unpack_from('<I', packed, 12)[0]
            storage = sum(b[3] in (12, 13) for b in struct.iter_unpack('<8i', packed[48:48+nb*32]))
            return {'key': key, 'supported_offline': True, 'source_spv_sha256': hashlib.sha256(raw).hexdigest(),
                    'vertex_storage_texture_bindings': storage, 'reduced_sampler_slots': removed, 'fragment_quad_approximation': quad_approximation, 'fragment_full_precision': struct.unpack_from('<I', packed, 8)[0] == 1}
        except (ValueError, OSError, KeyError, struct.error, subprocess.SubprocessError) as e:
            (out/(key+'.unsupported')).write_text(str(e)+'\n')
            return {'key': key, 'supported_offline': False, 'reason': str(e)}

    with concurrent.futures.ThreadPoolExecutor(a.jobs) as executor:
        records = list(executor.map(translate, metadata))
    failures = sum(not r['supported_offline'] for r in records)
    report = {'schema': 8 if a.buffer_storage or a.low_texture_budget else 7, 'backend': 'gles-experiment', 'glsl_es': 320,
              'source_index_sha256': hashlib.sha256((source/'shader_index.bin').read_bytes()).hexdigest(),
              'compiler_sha256': hashlib.sha256(cross.read_bytes()).hexdigest(),
              'programs': len(records), 'offline_unsupported': failures,
              'vertex_storage_texture_programs': sum(r.get('vertex_storage_texture_bindings', 0) > 0 for r in records),
              'filter_unsupported': a.filter_unsupported, 'runtime_compile_and_visual_validation_required': True,
              'records': records}
    (out/'gles-manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k != 'records'}))
    return 1 if failures and not a.filter_unsupported else 0


if __name__ == '__main__':
    raise SystemExit(main())
