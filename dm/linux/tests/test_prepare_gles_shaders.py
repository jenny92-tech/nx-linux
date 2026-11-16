# INPUT: Small synthetic SPIR-V interfaces and real metadata layout contracts.
# OUTPUT: Binding namespace, draw-uniform and malformed-input regressions.
# POS: Host checks for the optional GLES preparation transform.
import struct
import sys
from pathlib import Path
import unittest
import tempfile
import shutil
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from prepare_gles_shaders import remap, draw_uniforms, vertex_window_modes, vertex_storage_textures, sampled_buffer_textures, independent_compute_groups, graphics_interfaces, reduced_sampler_budget, fragment_quad_shuffle, compute_workgroup_shuffle, fragment_full_precision


class GlesPreparation(unittest.TestCase):
    def test_index_only_source_is_rejected_before_creating_output(self):
        from prepare_gles_shaders import main
        with tempfile.TemporaryDirectory() as work:
            root=Path(work);source=root/'source';source.mkdir();out=root/'prepared'
            (source/'shader_index.bin').write_bytes(b'index without translated stages')
            with patch.object(sys,'argv',['prepare','--source',str(source),'--out',str(out),'--spirv-cross',shutil.which('false')]):
                with self.assertRaises(SystemExit):main()
            self.assertFalse(out.exists())
    def test_unsupported_program_retains_original_dependency_facts(self):
        from prepare_gles_shaders import main
        with tempfile.TemporaryDirectory() as work:
            root=Path(work);source=root/'source';source.mkdir();out=root/'prepared'
            (source/'shader_index.bin').write_bytes(b'index fixture')
            header=struct.pack('<12I',0x4d4b5644,1,5,1,0,0,0,1,1,1,0,0)
            binding=struct.pack('<8i',0,2,7,3,4,0,1,1)
            original=header+binding;(source/'0123456789abcdef.vkmeta').write_bytes(original)
            (source/'0123456789abcdef.spv').write_bytes(struct.pack('<5I',0x07230203,0x10000,0,4,0))
            with patch.object(sys,'argv',['prepare','--source',str(source),'--out',str(out),'--spirv-cross',shutil.which('false'),'--filter-unsupported','--jobs','1']):
                self.assertEqual(main(),0)
            self.assertTrue((out/'0123456789abcdef.unsupported').is_file())
            self.assertFalse((out/'0123456789abcdef.glmeta').exists())
            self.assertEqual((out/'0123456789abcdef.glfacts').read_bytes(),original)

    def test_stage_qualified_binding(self):
        header=struct.pack('<12I',0x4d4b5644,1,1,1,0,0,0,1,1,1,0,0)
        binding=struct.pack('<8i',1,37,6,2,3,0,1,0)
        module=struct.pack('<9I',0x07230203,0x10000,0,4,0,4<<16|71,2,33,37)
        meta,spv=remap(header+binding,module)
        self.assertEqual(struct.unpack_from('<I',meta,4)[0],7)
        self.assertEqual(struct.unpack_from('<i',meta,48)[0],32)
        self.assertEqual(struct.unpack('<9I',spv)[-1],32)

    def test_draw_parameters_remain_dynamic_uniforms(self):
        words=[0x07230203,0x10000,0,10,0,
               2<<16|17,4427,
               6<<16|15,0,9,0x6e69616d,0,4,
               4<<16|71,4,11,4425,
               4<<16|21,1,32,1,
               4<<16|32,2,1,1,
               4<<16|59,2,4,1,
               4<<16|61,1,6,4]
        out=draw_uniforms(words)
        ops=[];at=5
        while at<len(out):n=out[at]>>16;ops.append(out[at:at+n]);at+=n
        self.assertIn([4<<16|59,10,4,0],ops)
        self.assertIn([4<<16|61,1,6,4],ops)
        self.assertIn([5<<16|15,0,9,0x6e69616d,0],ops)
        self.assertFalse(any(a[0]&65535 in (43,50) for a in ops))

    def test_malformed_metadata_rejected(self):
        for meta in (b'',b'\0'*47):
            with self.assertRaises(ValueError):remap(meta,b'')

    def test_malformed_instruction_rejected(self):
        with self.assertRaises(ValueError):draw_uniforms([0x07230203,0,0,10,0,0])

    def test_unmodified_module_stays_exact(self):
        words=[0x07230203,0x10000,0,4,0,2<<16|17,1]
        self.assertEqual(draw_uniforms(words),words)

    def test_window_modes_inside_vertex_main(self):
        source='#version 320 es\nvoid helper() {}\nvoid main() { if (true) { gl_Position=vec4(1); } }\n'
        out=vertex_window_modes(source)
        self.assertIn('void helper() {}',out)
        self.assertLess(out.index('uniform highp float dm_OriginY;'),out.index('void main()'))
        self.assertGreater(out.index('gl_Position.y *= dm_OriginY;'),out.index('gl_Position=vec4(1);'))
        self.assertTrue(out.rstrip().endswith('}'))

    def test_unformatted_image_without_binding_is_rejected(self):
        header=struct.pack('<12I',0x4d4b5644,1,5,0,0,0,0,1,1,1,0,0)
        words=[0x07230203,0x10000,0,4,0,9<<16|25,2,1,1,0,0,0,2,0]
        with self.assertRaisesRegex(ValueError,'binding metadata'):
            remap(header,struct.pack('<%dI'%len(words),*words))

    def test_unknown_storage_format_preserves_type_shape_and_slot(self):
        for scalar, declaration, dim, array, placeholder in (
            (0,[3<<16|22,1,32],2,0,1),
            (0,[3<<16|22,1,32],1,1,1),
            (1,[4<<16|21,1,32,1],1,0,21),
            (2,[4<<16|21,1,32,0],1,0,30),
        ):
            header=struct.pack('<12I',0x4d4b5644,1,5,1,0,0,0,1,1,1,0,0)
            binding=struct.pack('<8i',0,7,3,6,4,-1,1,4)
            words=[0x07230203,0x10000,0,8,0,4<<16|71,4,33,7]+declaration+[
                9<<16|25,2,1,dim,0,array,0,2,0,
                4<<16|32,3,0,2,4<<16|59,3,4,0]
            meta,out=remap(header+binding,struct.pack('<%dI'%len(words),*words))
            b=struct.unpack_from('<8i',meta,48)
            self.assertEqual((b[0],b[3],b[4],b[7]),(0,15,4,scalar|(dim<<4)|(array<<8)))
            result=struct.unpack('<%dI'%(len(out)//4),out)
            at=result.index(9<<16|25)
            self.assertEqual(result[at+8],placeholder)

    def test_vertex_storage_reads_preserve_index(self):
        header=struct.pack('<12I',0x4d4c4744,3,0,1,0,0,0,1,1,1,0,0)
        binding=struct.pack('<8i',0,1,7,3,4,0,1,0)
        source='layout(binding = 0, std430) buffer vp_s0 { uint _m0[]; } vp_s0_1;\nvoid main() { uint x=vp_s0_1._m0[int(a[b]+4)]; }'
        meta,out=vertex_storage_textures(header+binding,source)
        self.assertIn('texelFetch(dm_StorageTexture_0, ivec2(offset & 2047, offset >> 11), 0).r',out)
        self.assertIn('dm_StorageLoad_0(int(int(a[b]+4)))',out)
        self.assertEqual(struct.unpack_from('<8i',meta,48)[2:5],(4,12,4))

    def test_vertex_storage_writes_are_rejected(self):
        header=struct.pack('<12I',0x4d4c4744,3,0,1,0,0,0,1,1,1,0,0)
        source='layout(binding = 0, std430) buffer vp_s0 { uint _m0[]; } vp_s0_1;\nvoid main() { vp_s0_1._m0[3] += 1u; }'
        for flags in (0,1):
            binding=struct.pack('<8i',0,1,7,3,4,0,1,flags)
            with self.assertRaisesRegex(ValueError,'read'):
                vertex_storage_textures(header+binding,source)

    def test_graphics_interfaces_match_without_sharing_resources(self):
        source='layout(binding = 0, std140) uniform support { vec4 data; } support_1;\nlayout(location = 2) out vec4 _27;\nvoid main() { _27=support_1.data; }'
        vertex=struct.pack('<12I',0x4d4c4744,4,0,0,0,0,0,1,1,1,0,0)
        fragment=struct.pack('<12I',0x4d4c4744,4,1,0,0,0,0,1,1,1,0,0)
        vs=graphics_interfaces(vertex,source);fs=graphics_interfaces(fragment,source.replace('out vec4','in vec4'))
        self.assertIn('out vec4 dm_Varying_2;',vs);self.assertIn('in vec4 dm_Varying_2;',fs)
        self.assertIn('dm_vs_support_1.data',vs);self.assertIn('dm_fs_support_1.data',fs)

    def test_buffer_sampling_preserves_index_size_and_numeric_type(self):
        for prefix, kind in [('',0),('i',1),('u',2)]:
            header=struct.pack('<12I',0x4d4c4744,5,1,1,0,0,0,1,1,1,0,0)
            binding=struct.pack('<8i',16,3,1,4,8,-1,1,2)
            source=('#version 310 es\n#extension GL_EXT_texture_buffer : require\n'
                    'layout(binding = 16) uniform highp '+prefix+'samplerBuffer original;\n'
                    'void main() { '+prefix+'vec4 x=texelFetch(original, int(a[b]+(c*4))); int n=textureSize(original); }')
            meta,out=sampled_buffer_textures(header+binding,source)
            self.assertIn(prefix+'sampler2D original;',out)
            self.assertIn('dm_BufferFetch_16(int(a[b]+(c*4)))',out)
            self.assertIn('int n=dm_BufferTexels_16;',out)
            self.assertNotIn('samplerBuffer',out)
            self.assertNotIn('GL_EXT_texture_buffer',out)
            b=struct.unpack_from('<8i',meta,48);self.assertEqual((b[0],b[3],b[4],b[7]),(16,14,8,kind))

    def test_buffer_sampler_unrecognized_use_is_rejected(self):
        header=struct.pack('<12I',0x4d4c4744,5,5,1,0,0,0,1,1,1,0,0)
        binding=struct.pack('<8i',0,1,4,4,8,0,1,0)
        source='layout(binding = 0) uniform samplerBuffer t;\nvoid main() { vec4 x=texture(t, 1.0); }'
        with self.assertRaisesRegex(ValueError,'untranslated'):
            sampled_buffer_textures(header+binding,source)

    def test_buffer_sampler_storage_is_read_only_and_preserves_source_slot(self):
        header=struct.pack('<12I',0x4d4c4744,7,1,2,0,0,0,1,1,1,0,0)
        storage=struct.pack('<8i',16,5,7,3,2,0,1,0)
        sampled=struct.pack('<8i',19,9,1,4,8,-1,1,2)
        source='layout(binding = 19) uniform highp usamplerBuffer original;\nvoid main() { uvec4 x=texelFetch(original, index); int n=textureSize(original); }'
        meta,text=sampled_buffer_textures(header+storage+sampled,source,storage=True)
        b=struct.unpack_from('<8i',meta,80)
        self.assertEqual((b[0],b[1],b[2],b[3],b[4],b[7]),(17,19,7,16,8,2))
        self.assertEqual(struct.unpack_from('<I',meta,4)[0],8)
        self.assertIn('readonly buffer dm_BufferStorage_19',text)
        self.assertIn('dm_BufferFetch_19(index)',text)
        self.assertIn('int n=dm_BufferTexels_19',text)
        self.assertNotIn('sampler',text)

    def test_quality_budget_keeps_surface_texture_and_argument_evaluation(self):
        header=struct.pack('<12I',0x4d4c4744,8,1,2,0,0,0,1,1,1,0,0)
        bindings=struct.pack('<16i',16,1,1,4,10,-1,1,0,17,2,1,4,39,-1,1,0)
        source=('layout(binding = 16) uniform sampler2D diffuse;\n'
                'layout(binding = 17) uniform samplerCubeShadow shadow;\n'
                'void main() { float x=texture(shadow, getCoordinate()); vec4 y=texture(diffuse, uv); }')
        meta,text,removed=reduced_sampler_budget(header+bindings,source,limit=1)
        self.assertEqual(removed,[39]);self.assertEqual(struct.unpack_from('<I',meta,12)[0],1)
        self.assertIn('texture(diffuse, uv)',text);self.assertIn('dm_ReducedSample_17( getCoordinate())',text)
        self.assertNotIn('samplerCubeShadow',text)
        self.assertTrue(struct.unpack_from('<I',meta,44)[0]&(1<<15))

    def test_quad_approximation_rejects_cross_invocation_compute_operations(self):
        source='#version 320 es\nfloat helper(float x) { return subgroupShuffle(x,gl_SubgroupInvocationID^1u); }\n'
        result=fragment_quad_shuffle(source)
        self.assertIn('dFdx(value)',result);self.assertIn('dm_QuadShuffle(x,dm_QuadLane()^1u)',result)
        with self.assertRaises(ValueError):fragment_quad_shuffle(source.replace('subgroupShuffle(x,gl_SubgroupInvocationID^1u)','subgroupBallot(true)'))

    def test_fragment_float_bit_payloads_keep_full_precision(self):
        meta=struct.pack('<12I',0x4d4c4744,8,1,0,0,0,0,1,1,1,0,0)
        source='precision mediump float;\nlowp vec4 color; highp uint bits;'
        self.assertEqual(fragment_full_precision(meta,source),'precision highp float;\nhighp vec4 color; highp uint bits;')
        compute=bytearray(meta);struct.pack_into('<I',compute,8,5)
        self.assertEqual(fragment_full_precision(compute,source),source)

    def test_compute_shuffle_requires_reconverged_participants(self):
        meta=struct.pack('<12I',0x4d4c4744,8,5,0,0,0,0,32,1,1,0,0)
        wrapper=('int shuffle(int value, int lane) { bool valid=true; '
                 'return valid ? floatBitsToInt(subgroupShuffle(intBitsToFloat(value), uint(lane))) : value; }\n')
        source='#version 320 es\n'+wrapper+'void main() { int value=shuffle(17,int(gl_SubgroupInvocationID)^1); }'
        lowered=compute_workgroup_shuffle(meta,source)
        self.assertIn('shared uint dm_ShuffleWords[32]',lowered)
        self.assertEqual(lowered.count('barrier();'),2)
        self.assertNotIn('subgroupShuffle',lowered)
        for body in ('if (condition) { int x=shuffle(1,2); }','if(condition) x=shuffle(1,2);','if (condition) return; int x=shuffle(1,2);','for(int i=0;i<4;i++){int x=shuffle(1,2);}'):
            with self.assertRaises(ValueError):compute_workgroup_shuffle(meta,'#version 320 es\n'+wrapper+'void main() {'+body+'}')
        with self.assertRaises(ValueError):compute_workgroup_shuffle(meta,source.replace('gl_SubgroupInvocationID','gl_SubgroupLtMask.x'))

    def test_independent_3d_group_keeps_logical_invocations(self):
        header=struct.pack('<12I',0x4d4c4744,7,5,0,0,0,0,8,8,8,0,0)
        source=('layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;\n'
                'void main() { uvec3 p=gl_WorkGroupID*gl_WorkGroupSize+gl_LocalInvocationID; uvec3 n=gl_NumWorkGroups; uint i=gl_LocalInvocationIndex; }')
        meta,out=independent_compute_groups(header,source)
        self.assertEqual(struct.unpack_from('<I',meta,44)[0],2<<10)
        self.assertIn('local_size_z = 2',out)
        self.assertIn('gl_WorkGroupID / uvec3(1u, 1u, 4u)',out)
        self.assertIn('gl_NumWorkGroups / uvec3(1u, 1u, 4u)',out)
        covered={(x,y,z+physical_group*2) for physical_group in range(4) for x in range(8) for y in range(8) for z in range(2)}
        self.assertEqual(covered,{(x,y,z) for x in range(8) for y in range(8) for z in range(8)})

    def test_group_barriers_and_shared_state_are_not_split(self):
        header=struct.pack('<12I',0x4d4c4744,7,5,0,0,0,0,8,8,8,0,0)
        for use in ('barrier();','atomicAdd(buffer[0],1);','subgroupBallot(true);','shared uint state[512];'):
            source='layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;\nvoid main() { '+use+' }'
            self.assertEqual(independent_compute_groups(header,source),(header,source))


if __name__=='__main__':unittest.main()
