// INPUT: Verified main-menu eligibility and controller buttons after keyboard routing.
// OUTPUT: A release-armed B exit dialog; A confirms, B cancels, other scenes pass through.
// POS: Linux host UI policy; never emits guest actions or saves on the game's behalf.
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct { unsigned active,armed,drain;uint64_t previous; } DmExitMenu;
static inline int dm_exit_menu_input(DmExitMenu *s,int allowed,uint64_t buttons){
    if(!allowed){int consumed=s->active||s->drain;s->active=s->armed=0;s->drain=consumed&&buttons;s->previous=buttons;return consumed;}
    if(s->drain){if(!buttons)s->drain=0;s->previous=buttons;return 1;}
    if(!s->active){
        if(!buttons)s->armed=1;
        if(s->armed&&(buttons&2)&&!(s->previous&2)){s->active=1;s->armed=0;s->previous=buttons;return 1;}
        s->previous=buttons;return 0;
    }
    if(!s->armed){if(!buttons)s->armed=1;s->previous=buttons;return 1;}
    uint64_t edge=buttons&~s->previous;s->previous=buttons;
    if(edge&2){s->active=s->armed=0;s->drain=1;return 1;}
    return edge&1?2:1;
}
int dm_linux_exit_menu_tick(void *window,int allowed,uint64_t buttons,int keyboard_owner);
void dm_linux_exit_menu_saving(void);
int dm_linux_exit_hint_extent(unsigned output_width,unsigned output_height,unsigned *width,unsigned *height);
void dm_linux_exit_hint_pixels(uint8_t *pixels,unsigned width,unsigned height,int bgra);
