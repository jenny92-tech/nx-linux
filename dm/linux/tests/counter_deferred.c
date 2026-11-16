// INPUT: Completed query values, unavailable intervals, resets and pool reuse.
// OUTPUT: Exact report sums; no reads for intervals discarded before observation.
// POS: Host regression for deferred counter publication, independent of Vulkan.
#include "../../gfx/nvn/nvn_vk_counter_deferred.h"
#include <assert.h>
#include <stdio.h>
static unsigned reads;static int unavailable;
static int read_value(void *user,uint64_t pool,uint32_t query,uint64_t *out){
    (void)user;reads++;if(unavailable)return 0;*out=pool+query;return 1;
}
int main(void){
    NvnDeferredCounter s={0};
    assert(nvn_counter_deferred_add(&s,100,7));unavailable=1;
    nvn_counter_deferred_reset(&s);assert(!reads&&!s.n&&!s.sum);
    assert(nvn_counter_deferred_add(&s,100,7));assert(nvn_counter_deferred_add(&s,200,8));
    assert(!nvn_counter_deferred_resolve(&s,100,0,read_value,NULL));assert(s.n==2&&!s.sum);
    unavailable=0;assert(nvn_counter_deferred_resolve(&s,100,0,read_value,NULL));assert(s.n==1&&s.sum==107);
    assert(nvn_counter_deferred_add(&s,100,9));
    assert(nvn_counter_deferred_resolve(&s,0,1,read_value,NULL));assert(!s.n&&s.sum==424);
    assert(nvn_counter_deferred_resolve(&s,0,1,read_value,NULL));assert(s.sum==424);
    nvn_counter_deferred_reset(&s);assert(!s.n&&!s.sum);
    uint64_t full=0;unsigned random=123456789;
    for(unsigned batch=0;batch<20000;batch++){
        uint64_t pool=batch%2?100:200;
        assert(nvn_counter_deferred_resolve(&s,pool,0,read_value,NULL));
        for(unsigned q=0;q<32;q++){
            random=random*1664525u+1013904223u;
            switch(random%5){
                case 0:nvn_counter_deferred_reset(&s);full=0;break;
                case 1:assert(nvn_counter_deferred_resolve(&s,0,1,read_value,NULL));assert(s.sum==full);break;
                case 2:break; // A timestamp does not observe sample counts.
                default:assert(nvn_counter_deferred_add(&s,pool,q));full+=pool+q;break;
            }
        }
    }
    assert(nvn_counter_deferred_resolve(&s,0,1,read_value,NULL));assert(s.sum==full);
    nvn_counter_deferred_reset(&s);
    for(unsigned i=0;i<NVN_DEFERRED_COUNTER_LIMIT;i++)assert(nvn_counter_deferred_add(&s,1,i));
    assert(!nvn_counter_deferred_add(&s,1,0));
    puts("deferred counters: discarded unavailable intervals, exact reports, pool reuse and capacity passed");
}
