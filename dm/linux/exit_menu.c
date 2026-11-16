// INPUT: Creator-thread exit input and the existing same-window host UI presenter.
// OUTPUT: Chinese main-menu exit affordance and a controller confirmation dialog.
// POS: Linux host UI only; small hint is permanent, full canvas exists only while confirming.
#include "exit_menu.h"
#include "exit_menu_glyphs.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

extern int dm_vk_linux_keyboard_size(void *,int *,int *);
extern int dm_vk_linux_keyboard_present(void *,const void *,int,int,int);
extern void dm_vk_linux_keyboard_close(void *);
static DmExitMenu menu;
static _Atomic int hint_visible;
static uint8_t *canvas;
static int width,height,drawn,saving;

static void rect(uint8_t *p,unsigned w,unsigned h,unsigned x,unsigned y,unsigned rw,unsigned rh,uint32_t color){
    for(unsigned py=y;py<y+rh&&py<h;py++)for(unsigned px=x;px<x+rw&&px<w;px++){
        uint8_t *d=p+((size_t)py*w+px)*4;d[0]=(uint8_t)(color>>16);d[1]=(uint8_t)(color>>8);d[2]=(uint8_t)color;d[3]=255;
    }
}
static void glyph(uint8_t *p,unsigned w,unsigned h,unsigned x,unsigned y,unsigned scale,const uint32_t rows[20]){
    for(unsigned py=0;py<20;py++)for(unsigned px=0;px<20;px++)if(rows[py]&(1u<<(19-px)))
        rect(p,w,h,x+px*scale,y+py*scale,scale,scale,0xf3dfad);
}
static void chinese(uint8_t *p,unsigned w,unsigned h,unsigned x,unsigned y,unsigned scale,const unsigned *ids,size_t count){
    for(size_t i=0;i<count;i++)glyph(p,w,h,x+(unsigned)i*22*scale,y,scale,exit_glyphs[ids[i]]);
}
static void button(uint8_t *p,unsigned w,unsigned h,unsigned x,unsigned y,unsigned scale,int a){
    static const uint8_t rows[2][7]={{14,17,17,31,17,17,17},{30,17,17,30,17,17,30}};
    for(unsigned py=0;py<7;py++)for(unsigned px=0;px<5;px++)if(rows[a?0:1][py]&(1u<<(4-px)))
        rect(p,w,h,x+px*scale,y+py*scale,scale,scale,0xf3dfad);
}
int dm_linux_exit_hint_extent(unsigned w,unsigned h,unsigned *rw,unsigned *rh){
    if(!atomic_load(&hint_visible)||w<240||h<160)return 0;
    unsigned scale=w>=800&&h>=480?2:1;*rw=128*scale;*rh=28*scale;return 1;
}
void dm_linux_exit_hint_pixels(uint8_t *p,unsigned w,unsigned h,int bgra){
    unsigned scale=h/28;if(!scale)return;
    rect(p,w,h,0,0,w,h,0x302a20);button(p,w,h,7*scale,9*scale,scale+1,0);
    static const unsigned label[]={EXIT_TUI,EXIT_CHU,EXIT_YOU,EXIT_XI};
    chinese(p,w,h,29*scale,4*scale,scale,label,4);
    if(bgra)for(size_t i=0;i<(size_t)w*h;i++){uint8_t r=p[4*i];p[4*i]=p[4*i+2];p[4*i+2]=r;}
}
static void close_dialog(void *window,int keyboard_owner){
    if(canvas&&!keyboard_owner)dm_vk_linux_keyboard_close(window);
    free(canvas);canvas=NULL;width=height=drawn=saving=0;
}
static int draw_dialog(void *window){
    if(!canvas){
        if(!dm_vk_linux_keyboard_size(window,&width,&height)||width<320||height<240||width>4096||height>4096)return 0;
        canvas=malloc((size_t)width*height*4);if(!canvas)return 0;
    }
    unsigned w=(unsigned)width,h=(unsigned)height,scale=w>=800&&h>=480?2:1;
    unsigned pw=300*scale,ph=150*scale,x=(w-pw)/2,y=(h-ph)/2;
    rect(canvas,w,h,0,0,w,h,0x0e1118);rect(canvas,w,h,x,y,pw,ph,0x65573c);rect(canvas,w,h,x+2,y+2,pw-4,ph-4,0x201e1a);
    static const unsigned title[]={EXIT_TUI,EXIT_CHU,EXIT_YOU,EXIT_XI,EXIT_QUESTION};
    static const unsigned detail[]={EXIT_FAN,EXIT_HUI,EXIT_ZHANG,EXIT_JI,EXIT_CAI,EXIT_DAN};
    static const unsigned wait[]={EXIT_ZHENG,EXIT_ZAI,EXIT_BAO,EXIT_CUN};
    static const unsigned yes[]={EXIT_QUE,EXIT_REN},no[]={EXIT_QU,EXIT_XIAO};
    chinese(canvas,w,h,x+96*scale,y+18*scale,scale,title,5);
    chinese(canvas,w,h,x+(saving?106:84)*scale,y+56*scale,scale,saving?wait:detail,saving?4:6);
    button(canvas,w,h,x+49*scale,y+111*scale,2*scale,1);chinese(canvas,w,h,x+72*scale,y+105*scale,scale,yes,2);
    button(canvas,w,h,x+177*scale,y+111*scale,2*scale,0);chinese(canvas,w,h,x+200*scale,y+105*scale,scale,no,2);
    return dm_vk_linux_keyboard_present(window,canvas,width,height,width*4);
}
void dm_linux_exit_menu_saving(void){saving=1;drawn=0;}
int dm_linux_exit_menu_tick(void *window,int allowed,uint64_t buttons,int keyboard_owner){
    int was_active=menu.active,result=dm_exit_menu_input(&menu,allowed,buttons);
    atomic_store(&hint_visible,allowed&&!menu.active&&!menu.drain);
    if(menu.active&&!drawn){
        if(!draw_dialog(window)){
            fprintf(stderr,"LINUX_EXIT_UI_FAILED: cannot present exit confirmation\n");
            menu.active=menu.armed=0;menu.drain=1;close_dialog(window,keyboard_owner);return 1;
        }
        drawn=1;
    }
    if(was_active&&!menu.active)close_dialog(window,keyboard_owner);
    if(saving&&menu.active&&menu.armed)result=2;
    return result;
}
