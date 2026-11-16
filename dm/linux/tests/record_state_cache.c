// INPUT: Independent reference state, random setters, child calls and cache mode generations.
// OUTPUT: Every draw observes identical state with redundant assignments removed.
// POS: Host sanitizer regression; mutable inputs are compared by value, never by pointer.
#include "../../gfx/nvn/nvn_record_state_cache.h"
#include <assert.h>
#include <stdio.h>
static NvnRecordStateCache cache;
int main(void){
    uint64_t reference[128]={0},optimized[128]={0},generation=1;uint32_t rng=0x7193cu;unsigned omitted=0;
    for(unsigned i=0;i<100000;i++){
        rng=rng*1664525u+1013904223u;unsigned key=(rng>>16)%128;uint64_t value=(rng>>8)%7;
        if(i%197==0){
            /* A child command buffer may change any state. */
            for(unsigned j=0;j<128;j++)reference[j]=optimized[j]=rng+j;
            nvn_record_state_invalidate(&cache);
        }
        if(i%257==0)generation++;
        reference[key]=value;
        if(i%13==0||nvn_record_state_changed(&cache,generation,key,&value,sizeof value))optimized[key]=value;
        else omitted++;
        /* A disabled setter must invalidate before cache use resumes. */
        if(i%13==0)generation++;
        if(i%7==0)assert(!memcmp(reference,optimized,sizeof reference));
    }
    assert(omitted>0&&!memcmp(reference,optimized,sizeof reference));
    uint8_t bytes[321]={0};assert(nvn_record_state_changed(&cache,++generation,7,bytes,320));
    assert(!nvn_record_state_changed(&cache,generation,7,bytes,320));
    bytes[319]=1;assert(nvn_record_state_changed(&cache,generation,7,bytes,320));
    nvn_record_state_invalidate(&cache);assert(nvn_record_state_changed(&cache,generation,7,bytes,320));
    assert(nvn_record_state_changed(&cache,generation,128,bytes,1));
    assert(nvn_record_state_changed(&cache,generation,7,bytes,321));
    /* Interleaved stage selection must not forget the previous VS/PS. */
    uint64_t a=100,b=200;assert(nvn_record_state_changed(&cache,generation,80,&a,8));
    assert(nvn_record_state_changed(&cache,generation,81,&b,8));
    assert(!nvn_record_state_changed(&cache,generation,80,&a,8));
    assert(!nvn_record_state_changed(&cache,generation,81,&b,8));
    assert(nvn_record_state_changed(&cache,++generation,80,&a,8));
    /* Outer setter selected A, then allocation calls code that may emit B.
     * Unknown state after that callback must prevent skipping the next B. */
    assert(nvn_record_state_changed(&cache,generation,81,&a,8));
    nvn_record_state_invalidate(&cache);
    assert(nvn_record_state_changed(&cache,generation,81,&b,8));
    puts("record state cache: 100000 reference draw states, calls, generations, mutable bytes, stages and bounds passed");
}
