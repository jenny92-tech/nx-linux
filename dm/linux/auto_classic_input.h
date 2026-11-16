// INPUT: Explicit classic preset, original target mode and this process's input.
// OUTPUT: Bounded original LT+Minus gesture, released after a guest sample.
// POS: One-shot per game entry; verified main menu rearms without emitting input.
#pragma once
#include <stdint.h>
typedef struct {uint32_t next,chord_at,until;unsigned seen_hd,done,holding,attempted,chord;uint64_t chord_sample;} DmAutoClassicInput;
static inline int dm_auto_classic_input(DmAutoClassicInput *s,int mode,uint32_t now,int activity,
    uint64_t sampled,uint64_t sampled_buttons,uint64_t *buttons){
    /* Only mode 2 identifies a verified main menu with no HUD/modal. Unknown
     * states and in-game settings must not undo a completed one-shot. */
    if(mode==2){*s=(DmAutoClassicInput){0};return 0;}
    if(s->done)return 0;
    if(mode==0){if(s->seen_hd)s->done=1;s->holding=0;return 0;}
    if(mode!=1){s->holding=0;return 0;}
    s->seen_hd=1;
    if(activity){s->holding=0;return 0;}
    if(s->holding&&(int32_t)(now-s->until)<0){
        if((int32_t)(now-s->chord_at)<0){*buttons|=0x100;return 0;}
        /* Hold the edge until the original HID reader sees it, then release
         * even if its graphics-mode update has not run yet. A held Minus can
         * otherwise repeat the toggle and keep the alternate skill bar up. */
        if(s->chord&&sampled>s->chord_sample&&(sampled_buttons&0x900)==0x900){s->holding=0;return 0;}
        if(!s->chord){s->chord=1;s->chord_sample=sampled;}
        *buttons|=0x900;return 0;
    }
    s->holding=0;
    if(s->attempted&&(int32_t)(now-s->next)<0)return 0;
    /* Establish the modifier before the Minus edge. Startup frames can be
     * much slower than 300 ms; retain each phase across ordinary input polls. */
    s->next=now+5000;s->chord_at=now+600;s->until=now+1100;
    s->chord=0;s->holding=s->attempted=1;*buttons|=0x100;return 1;
}
