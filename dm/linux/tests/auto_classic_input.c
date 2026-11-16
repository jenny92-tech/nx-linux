// INPUT: Original target-mode transitions, physical activity and wrapped SDL time.
// OUTPUT: Success disarms; only a verified main menu rearms the bounded gesture.
// POS: Host input policy regression; no game library, SDL or input device required.
#include "auto_classic_input.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    DmAutoClassicInput s={0};uint64_t b=0;
    assert(!dm_auto_classic_input(&s,-1,100,0,0,0,&b)&&!b);
    assert(!dm_auto_classic_input(&s,0,101,0,0,0,&b)&&!s.done);
    assert(!dm_auto_classic_input(&s,1,102,1,0,0,&b)&&!b);
    assert(dm_auto_classic_input(&s,1,103,0,0,0,&b)&&b==0x100);
    b=0;assert(!dm_auto_classic_input(&s,1,200,0,0,0,&b)&&b==0x100);
    b=0;assert(!dm_auto_classic_input(&s,1,702,0,0,0,&b)&&b==0x100);
    b=0;assert(!dm_auto_classic_input(&s,1,703,0,0,0,&b)&&b==0x900);
    b=0;assert(!dm_auto_classic_input(&s,1,1100,0,0,0,&b)&&b==0x900);
    b=1;assert(!dm_auto_classic_input(&s,1,1110,1,0,0,&b)&&b==1);
    b=0;assert(!dm_auto_classic_input(&s,1,1204,0,0,0,&b)&&!b);
    assert(dm_auto_classic_input(&s,1,5103,0,0,0,&b)&&b==0x100);
    b=0;assert(!dm_auto_classic_input(&s,0,5150,0,0,0,&b)&&!b&&s.done);
    assert(!dm_auto_classic_input(&s,1,15000,0,0,0,&b)&&!b);
    assert(!dm_auto_classic_input(&s,-1,15001,0,0,0,&b)&&s.done&&!b);
    assert(!dm_auto_classic_input(&s,0,15002,0,0,0,&b)&&s.done&&!b);
    b=1;assert(!dm_auto_classic_input(&s,2,15003,1,0,0,&b)&&!s.done&&b==1);
    assert(!s.attempted&&!s.holding&&!s.seen_hd);
    b=0;assert(!dm_auto_classic_input(&s,-1,15004,0,0,0,&b)&&!b);
    assert(dm_auto_classic_input(&s,1,15005,0,0,0,&b)&&b==0x100);
    b=0;assert(!dm_auto_classic_input(&s,0,15006,0,0,0,&b)&&s.done&&!b);
    s=(DmAutoClassicInput){.next=0xfffffff0};
    assert(dm_auto_classic_input(&s,1,0xfffffff0,0,0,0,&b));
    b=0;assert(!dm_auto_classic_input(&s,1,20,0,0,0,&b)&&b==0x100);
    b=0;assert(!dm_auto_classic_input(&s,1,600,0,0,0,&b)&&b==0x900);
    b=0;assert(!dm_auto_classic_input(&s,1,1100,0,0,0,&b)&&!b);
    s=(DmAutoClassicInput){0};b=0;
    assert(dm_auto_classic_input(&s,1,0xfffffff0,0,0,0,&b)&&b==0x100);
    b=0;assert(!dm_auto_classic_input(&s,2,30,0,0,0,&b)&&!b&&!s.holding&&!s.attempted);
    assert(!dm_auto_classic_input(&s,-1,31,0,0,0,&b)&&!b);
    /* The guest may consume the chord before its graphics update runs.
     * Release it on that sample instead of toggling throughout the hold. */
    s=(DmAutoClassicInput){0};b=0;
    assert(dm_auto_classic_input(&s,1,100,0,10,0,&b)&&b==0x100);
    b=0;assert(!dm_auto_classic_input(&s,1,700,0,10,0,&b)&&b==0x900);
    b=0;assert(!dm_auto_classic_input(&s,1,705,0,10,0x900,&b)&&b==0x900);
    b=0;assert(!dm_auto_classic_input(&s,1,710,0,11,0x900,&b)&&!b&&!s.holding);
    b=0;assert(!dm_auto_classic_input(&s,1,800,0,12,0,&b)&&!b&&!s.done);
    b=0;assert(!dm_auto_classic_input(&s,0,900,0,13,0,&b)&&!b&&s.done);
    puts("auto classic: guest-acknowledged release, priority, rearm and timer wrap passed");
}
