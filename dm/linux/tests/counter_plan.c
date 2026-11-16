// INPUT: Exhaustive and deterministic multi-batch counter command sequences.
// OUTPUT: Every observable report and cross-batch tail matches full counting.
// POS: Host-side semantic check for eliminating overwritten occlusion results.
#include "nvn_vk_counter_plan.h"
#include <assert.h>
#include <stdio.h>

typedef struct { op_hdr h;uint64_t type,address; } Command;
static unsigned checked;
static uint64_t random_state=UINT64_C(0x7be8346dc910f025);
static unsigned random_next(void){random_state^=random_state<<13;random_state^=random_state>>7;random_state^=random_state<<17;return (unsigned)random_state;}
static void check(const unsigned *types,size_t n,uint64_t *full,uint64_t *optimized){
    Command commands[96];const uint8_t *ops[96];uint8_t live[96];assert(n<=96);
    for(size_t i=0;i<n;i++){
        commands[i]=(Command){.h={.op=OP_DRAW,.size=8}};
        if(types[i]==2)commands[i]=(Command){.h={.op=OP_RESET_COUNTER,.size=16},.type=NVN_COUNTER_TYPE_SAMPLES_PASSED};
        if(types[i]>=3)commands[i]=(Command){.h={.op=OP_REPORT_COUNTER,.size=24},.type=types[i]==3?NVN_COUNTER_TYPE_SAMPLES_PASSED:types[i]==4?NVN_COUNTER_TYPE_TIMESTAMP:NVN_COUNTER_TYPE_TIMESTAMP_TOP};
        ops[i]=(const uint8_t *)&commands[i];
    }
    assert(nvn_counter_plan(ops,n,live));
    for(size_t i=0;i<n;i++){
        if(types[i]<2){uint64_t samples=types[i]?17:1;*full+=samples;if(live[i])*optimized+=samples;}
        else if(types[i]==2)*full=*optimized=0;
        else if(types[i]==3)assert(*full==*optimized);
    }
    assert(*full==*optimized); /* A future batch may read without resetting. */
    checked++;
}
int main(void){
    unsigned types[96];
    for(unsigned code=0;code<279936;code++){
        unsigned value=code;for(size_t i=0;i<7;i++){types[i]=value%6;value/=6;}
        uint64_t full=13,optimized=13;check(types,7,&full,&optimized);
    }
    uint64_t full=0,optimized=0;
    for(unsigned batch=0;batch<50000;batch++){
        size_t n=random_next()%96+1;for(size_t i=0;i<n;i++)types[i]=random_next()%6;
        check(types,n,&full,&optimized);
    }
    Command bad={.h={.op=OP_REPORT_COUNTER,.size=24},.type=99};
    const uint8_t *ops[]={(const uint8_t *)&bad};uint8_t live[1];
    assert(!nvn_counter_plan(ops,1,live));bad.type=NVN_COUNTER_TYPE_SAMPLES_PASSED;bad.h.size=16;
    assert(!nvn_counter_plan(ops,1,live));bad.h.op=OP_RESET_COUNTER;bad.h.size=8;
    assert(!nvn_counter_plan(ops,1,live));assert(nvn_counter_plan(NULL,0,NULL));
    Command prefix[3]={
        {.h={OP_REPORT_COUNTER,24},.type=NVN_COUNTER_TYPE_TIMESTAMP},
        {.h={OP_RESET_COUNTER,16},.type=NVN_COUNTER_TYPE_SAMPLES_PASSED},
        {.h={OP_REPORT_COUNTER,24},.type=NVN_COUNTER_TYPE_SAMPLES_PASSED}};
    const uint8_t *first[]={(const uint8_t *)&prefix[0],(const uint8_t *)&prefix[1],(const uint8_t *)&prefix[2]};
    assert(nvn_counter_prefix_reset(first,3));
    prefix[0].type=NVN_COUNTER_TYPE_SAMPLES_PASSED;assert(!nvn_counter_prefix_reset(first,3));
    prefix[0].type=99;assert(!nvn_counter_prefix_reset(first,3));
    prefix[0].type=NVN_COUNTER_TYPE_TIMESTAMP;prefix[1].h.size=8;assert(!nvn_counter_prefix_reset(first,3));
    printf("counter liveness: %u sequences preserve reports and cross-batch state\n",checked);
    return 0;
}
