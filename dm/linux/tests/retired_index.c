// INPUT: Synthetic duplicate/colliding keys, random insertion and swap removal.
// OUTPUT: Every live slot indexed exactly once and only exact size/usage matches.
// POS: Host ASan/UBSan ownership-neutral cache-index regression.
#include "../../gfx/nvn/nvn_vk_retired_index.h"
#include <assert.h>
#include <stdio.h>
static NvnRetiredIndex s;
static uint64_t sizes[NVN_RETIRED_CAPACITY];static uint32_t uses[NVN_RETIRED_CAPACITY];
static void check(void){
    unsigned seen[NVN_RETIRED_CAPACITY]={0},total=0;
    for(unsigned b=0;b<NVN_RETIRED_BUCKETS;b++)for(unsigned p=s.head[b];p;){
        unsigned i=p-1;assert(i<s.n&&!seen[i]++);total++;
        assert(s.size[i]==sizes[i]&&s.usage[i]==uses[i]&&nvn_retired_bucket(sizes[i],uses[i])==b);p=s.next[i];
    }
    assert(total==s.n);
    for(unsigned i=0;i<s.n;i++){unsigned at=nvn_retired_find(&s,sizes[i],uses[i],NULL);assert(at<s.n&&sizes[at]==sizes[i]&&uses[at]==uses[i]);}
}
int main(void){
    unsigned n=0;uint32_t rng=0x76a94u;
    for(unsigned turn=0;turn<100000;turn++){
        rng=rng*1664525u+1013904223u;
        if(n<NVN_RETIRED_CAPACITY&&(!n||(rng&3))){
            sizes[n]=(rng%33+1)*16;uses[n]=(rng>>12)%7+1;
            assert(nvn_retired_add(&s,n,sizes[n],uses[n]));n++;
        }else{unsigned at=(rng>>3)%n;assert(nvn_retired_remove_move(&s,at));n--;sizes[at]=sizes[n];uses[at]=uses[n];}
        assert(n==s.n);if(turn%127==0)check();
        assert(nvn_retired_find(&s,UINT64_MAX,UINT32_MAX,NULL)==UINT32_MAX);
    }
    check();while(n){assert(nvn_retired_remove_move(&s,n-1));n--;}check();
    assert(!nvn_retired_remove_move(&s,0)&&!nvn_retired_add(&s,1,16,1));
    puts("retired index: 100000 randomized duplicate/collision/swap operations and exact membership passed");
}
