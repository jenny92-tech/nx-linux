#version 450
#extension GL_KHR_shader_subgroup_shuffle : require
layout(set=0,binding=0,std430) buffer Values {uint values[256];};
uint payload(uint i){
    const uint cases[16]=uint[16](0u,0x80000000u,0x7f800000u,0xff800000u,0x7fc01234u,0x7f801234u,0xffffffffu,1u,
        0xffff0000u,0xffffu,0x80008000u,0x7fff7fffu,0x1234abcdu,0xabcd1234u,0x3f800000u,0xbf800000u);
    return i<16u?cases[i]:0x9e3779b9u*(i+1u);
}
void main(){
    uint i=uint(gl_FragCoord.x)+8u*uint(gl_FragCoord.y);
    // Generate payloads from invocation coordinates; the real material
    // exchanges interpolated inputs rather than storage-buffer values.
    float value=uintBitsToFloat(payload(i));
    values[128u+i]=floatBitsToUint(subgroupShuffleXor(value,2u));
    values[192u+i]=floatBitsToUint(subgroupShuffleXor(value,1u));
}
