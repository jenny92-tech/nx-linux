// INPUT: Decimal control tokens at the unsigned 32/64-bit boundaries.
// OUTPUT: Exact flags without signed conversion or overflow acceptance.
// POS: Regression for the shared-alias optimization's high flag bit.
#include "../../gfx/nvn/nvn_vk_perf_flags.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    unsigned long long sequence=0;unsigned flags=0;
    assert(nvn_vk_perf_flags_parse("1 4292739279\n",&sequence,&flags)&&sequence==1&&flags==4292739279u);
    assert(nvn_vk_perf_flags_parse("18446744073709551615 4294967295",&sequence,&flags)&&sequence==UINT64_MAX&&flags==UINT32_MAX);
    assert(nvn_vk_perf_flags_parse(" \t3 2147483648 \r\n",&sequence,&flags)&&sequence==3&&flags==2147483648u);
    const char *bad[]={"", "1", "1 -1", "1 +1", "-1 1", "+1 1", "1 4294967296",
        "18446744073709551616 1", "1 18446744073709551616", "1 1 extra", "1 0x10", "1.0 1"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++)assert(!nvn_vk_perf_flags_parse(bad[i],&sequence,&flags));
    assert(!nvn_vk_perf_flags_parse(NULL,&sequence,&flags));
    puts("performance flags: unsigned high bit, exact 32/64-bit limits, signs, overflow and trailing-token guards passed");
}
