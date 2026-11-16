#version 450
// The real driver bank layout: vec4[4096], bank 2, static word 0x168.
layout(set=1,binding=3,std140) uniform Driver {vec4 data[4096];} driver;
layout(set=0,binding=0,std430) buffer Values {uint values[256];};
void main(){
    uint i=uint(gl_FragCoord.x)+8u*uint(gl_FragCoord.y);
    values[128u+i]=floatBitsToUint(driver.data[22].z);
}
