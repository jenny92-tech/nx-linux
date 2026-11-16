// INPUT: Main-menu transitions, held buttons, UI failures and native-size canvases.
// OUTPUT: Confirmation, cancellation, release drain and bounded Chinese raster checks.
// POS: Host-only exit UI regression; no game, device, driver or saves required.
#include "../exit_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,presents,closes;static int fail,width=960,height=720;
static uint8_t frame[960*720*4];
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"exit menu failed line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int dm_vk_linux_keyboard_size(void *window,int *w,int *h){(void)window;*w=width;*h=height;return 1;}
int dm_vk_linux_keyboard_present(void *window,const void *p,int w,int h,int pitch){
    (void)window;CHECK(w==width&&h==height&&pitch==w*4);presents++;if(w==960&&h==720)memcpy(frame,p,sizeof frame);return !fail;
}
void dm_vk_linux_keyboard_close(void *window){(void)window;closes++;}
static void state(void){
    DmExitMenu s={0};
    CHECK(!dm_exit_menu_input(&s,0,2)&&!s.active);
    CHECK(!dm_exit_menu_input(&s,1,2)&&!s.active); // Held B across a scene change.
    CHECK(!dm_exit_menu_input(&s,1,0));CHECK(dm_exit_menu_input(&s,1,2)==1&&s.active);
    CHECK(dm_exit_menu_input(&s,1,3)==1); // Held opening B plus A cannot confirm.
    CHECK(dm_exit_menu_input(&s,1,0)==1);CHECK(dm_exit_menu_input(&s,1,1)==2);
    CHECK(dm_exit_menu_input(&s,1,1)==1);CHECK(dm_exit_menu_input(&s,1,0)==1);
    CHECK(dm_exit_menu_input(&s,1,3)==1&&!s.active); // Cancel wins simultaneous A+B.
    CHECK(dm_exit_menu_input(&s,1,3)==1);CHECK(dm_exit_menu_input(&s,1,0)==1);
    CHECK(!dm_exit_menu_input(&s,1,0));CHECK(dm_exit_menu_input(&s,1,2)==1);
    CHECK(dm_exit_menu_input(&s,0,2)==1&&!s.active); // Focus/keyboard/modal change cancels.
    CHECK(dm_exit_menu_input(&s,1,2)==1);CHECK(dm_exit_menu_input(&s,1,0)==1);
    CHECK(!dm_exit_menu_input(&s,1,0));
    CHECK(!dm_exit_menu_input(&s,0,1));CHECK(!dm_exit_menu_input(&s,0,2));
}
int main(int argc,char **argv){
    state();unsigned w,h;
    CHECK(!dm_linux_exit_menu_tick(NULL,1,0,0));CHECK(dm_linux_exit_hint_extent(960,720,&w,&h)&&w==256&&h==56);
    uint8_t *p=malloc((size_t)w*h*4+32);CHECK(p);memset(p,0x5c,(size_t)w*h*4+32);
    dm_linux_exit_hint_pixels(p,w,h,0);for(unsigned i=0;i<32;i++)CHECK(p[(size_t)w*h*4+i]==0x5c);
    CHECK(p[0]==0x30&&p[1]==0x2a&&p[2]==0x20&&p[3]==255);
    dm_linux_exit_hint_pixels(p,w,h,1);CHECK(p[0]==0x20&&p[2]==0x30);free(p);
    CHECK(dm_linux_exit_menu_tick(NULL,1,2,0)==1&&presents==1);CHECK(!dm_linux_exit_hint_extent(960,720,&w,&h));
    if(argc==2){FILE *f=fopen(argv[1],"wb");CHECK(f);fprintf(f,"P6\n960 720\n255\n");for(size_t i=0;i<960*720;i++)CHECK(fwrite(frame+4*i,1,3,f)==3);CHECK(!fclose(f));}
    CHECK(dm_linux_exit_menu_tick(NULL,1,0,0)==1);CHECK(dm_linux_exit_menu_tick(NULL,1,1,0)==2);
    dm_linux_exit_menu_saving();CHECK(dm_linux_exit_menu_tick(NULL,1,0,0)==2&&presents==2);
    CHECK(dm_linux_exit_menu_tick(NULL,1,2,0)==1&&closes==1);CHECK(dm_linux_exit_menu_tick(NULL,1,0,0)==1);
    CHECK(!dm_linux_exit_menu_tick(NULL,1,0,0));fail=1;CHECK(dm_linux_exit_menu_tick(NULL,1,2,0)==1&&closes==2);
    CHECK(dm_linux_exit_menu_tick(NULL,1,0,0)==1);CHECK(!dm_linux_exit_menu_tick(NULL,1,0,0));
    fail=0;CHECK(dm_linux_exit_menu_tick(NULL,1,2,0)==1);CHECK(dm_linux_exit_menu_tick(NULL,1,0,0)==1);
    unsigned before=closes;CHECK(dm_linux_exit_menu_tick(NULL,0,0,1)==1);CHECK(closes==before); // Real keyboard now owns the shared presenter.
    CHECK(!dm_linux_exit_menu_tick(NULL,1,0,0));width=8000;CHECK(dm_linux_exit_menu_tick(NULL,1,2,0)==1&&presents==4);
    CHECK(dm_linux_exit_menu_tick(NULL,0,0,0)==1);
    printf("EXIT_MENU_OK checks=%u confirmation_cancel_release_focus_raster_failure=1\n",checks);
}
