// INPUT: Guest keyboard requests and SDL events on the window creator thread.
// OUTPUT: Software-rendered controller keyboard; guest receives text only on OK.
// POS: Linux applet replacement. Never replaces the live game's Vulkan window.
#include "keyboard_sdl.h"
#include "keyboard_model.h"
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready=PTHREAD_COND_INITIALIZER;
static DmLinuxKeyboard model;
static int active,drain,armed,dirty;
static uint64_t previous;
static uint32_t repeat_at,capture_id;
static SDL_Window *owner;
static SDL_Surface *canvas;
static SDL_Renderer *renderer;
extern int dm_vk_linux_keyboard_size(void *,int *,int *);
extern int dm_vk_linux_keyboard_present(void *,const void *,int,int,int);
extern void dm_vk_linux_keyboard_close(void *);

int dm_linux_keyboard_request(const char *initial,int max_units,int name,int numeric_max,char *out,int capacity){
    if(!out||capacity<1)return 0;
    pthread_mutex_lock(&lock);
    if(active){pthread_mutex_unlock(&lock);return 0;}
    dm_kbd_init(&model,initial,max_units,name,numeric_max);
    active=dirty=1;armed=0;previous=0;repeat_at=0;
    fprintf(stderr,"LINUX_KEYBOARD opened controller_ui=1 name_rules=%d max_units=%d\n",name,max_units);
    while(active)pthread_cond_wait(&ready,&lock);
    int result=model.result==1;
    if(result){size_t n=strlen(model.text);if(n>=(size_t)capacity)result=0;else memcpy(out,model.text,n+1);}
    fprintf(stderr,"LINUX_KEYBOARD closed accepted=%d bytes=%zu\n",result,result?strlen(model.text):0);
    pthread_mutex_unlock(&lock);return result;
}
int dm_linux_keyboard_active(void){pthread_mutex_lock(&lock);int value=active||drain;pthread_mutex_unlock(&lock);return value;}

/* Hand-authored 5x7 glyphs, row bits from left to right. No font/runtime dependency. */
static const uint8_t alphabet[26][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,14},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,25,21,19,19,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}};
static const uint8_t digits[10][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14}};
static const uint8_t lowercase[26][7]={
 {0,0,14,1,15,17,15},{16,16,30,17,17,17,30},{0,0,14,16,16,17,14},
 {1,1,15,17,17,17,15},{0,0,14,17,31,16,14},{6,8,8,30,8,8,8},
 {0,0,15,17,15,1,14},{16,16,30,17,17,17,17},{4,0,12,4,4,4,14},
 {2,0,6,2,2,18,12},{16,16,18,20,24,20,18},{12,4,4,4,4,4,14},
 {0,0,26,21,21,21,21},{0,0,30,17,17,17,17},{0,0,14,17,17,17,14},
 {0,0,30,17,30,16,16},{0,0,15,17,15,1,1},{0,0,22,25,16,16,16},
 {0,0,15,16,14,1,30},{8,8,30,8,8,9,6},{0,0,17,17,17,19,13},
 {0,0,17,17,17,10,4},{0,0,17,17,21,21,10},{0,0,17,10,4,10,17},
 {0,0,17,17,15,1,14},{0,0,31,2,4,8,31}};
static void color(int r,int g,int b){SDL_SetRenderDrawColor(renderer,(Uint8)r,(Uint8)g,(Uint8)b,255);}
static void box(int x,int y,int w,int h){SDL_Rect rect={x,y,w,h};SDL_RenderFillRect(renderer,&rect);}
static void text(int x,int y,const char *s,int scale){
    for(;*s;s++,x+=6*scale){
        unsigned char c=(unsigned char)*s;const uint8_t *rows=NULL;uint8_t special[7]={0};
        if(c>='a'&&c<='z')rows=lowercase[c-'a'];
        else if(c>='A'&&c<='Z')rows=alphabet[c-'A'];
        else if(c>='0'&&c<='9')rows=digits[c-'0'];
        else {switch(c){
            case '-':special[3]=31;break;case '_':special[6]=31;break;
            case '.':special[6]=4;break;case ':':special[2]=special[5]=4;break;
            case '/':special[1]=1;special[2]=2;special[3]=4;special[4]=8;special[5]=16;break;
            case '+':special[2]=special[4]=4;special[3]=14;break;
            case ' ':break;default:special[0]=special[6]=31;special[1]=special[5]=17;break;
        }rows=special;}
        for(int row=0;row<7;row++)for(int col=0;col<5;col++)if(rows[row]&(1u<<(4-col)))
            box(x+col*scale,y+row*scale,scale,scale);
    }
}
static void draw(void){
    SDL_RenderSetLogicalSize(renderer,960,720);color(14,17,24);SDL_RenderClear(renderer);
    color(231,197,119);text(42,38,model.name?"CHARACTER NAME":"TEXT INPUT",4);
    color(55,65,80);box(38,96,884,78);color(248,248,240);
    char display[45];size_t n=strlen(model.text);snprintf(display,sizeof display,"%s",model.text+(n>40?n-40:0));
    text(56,123,*display?display:"_",3);
    color(196,203,214);text(42,190,model.name?"2-15 LETTERS. OPTIONAL ONE - OR _.":"USE CONTROLLER OR A CONNECTED KEYBOARD",2);
    static const char rows[4][11]={"ABCDEFGHIJ","KLMNOPQRST","UVWXYZ-_ .","0123456789"};
    for(int row=0;row<4;row++)for(int col=0;col<10;col++){
        int selected=model.row==row&&model.col==col;
        color(selected?142:39,selected?111:46,selected?58:59);box(40+col*88,236+row*66,80,56);
        char glyph[]={rows[row][col],0};if(model.lower&&glyph[0]>='A'&&glyph[0]<='Z')glyph[0]+='a'-'A';
        color(248,244,225);text(70+col*88,253+row*66,glyph,3);
    }
    static const char *labels[]={"DEL","CASE","SPACE","OK","CANCEL"};
    for(int i=0;i<5;i++){
        int selected=model.row==4&&model.col/2==i;
        color(selected?142:39,selected?111:46,selected?58:59);box(40+i*176,505,168,54);
        color(248,244,225);text(57+i*176,523,labels[i],3);
    }
    color(246,145,112);text(42,583,model.error,2);
    color(214,220,230);text(42,624,"D-PAD/STICK MOVE   A TYPE   X DELETE",2);
    text(42,655,"START OK   B CANCEL   SELECT/Y CASE",2);
    color(231,197,119);text(720,190,model.lower?"LOWER CASE":"UPPER CASE",2);
    SDL_RenderPresent(renderer);
    if(!dm_vk_linux_keyboard_present(owner,canvas->pixels,canvas->w,canvas->h,canvas->pitch))model.result=-1;
    dirty=0;
}
int dm_linux_keyboard_event(const SDL_Event *e){
    pthread_mutex_lock(&lock);int consumed=active||drain;
    if(active){
        if(e->type==SDL_TEXTINPUT){dm_kbd_append(&model,e->text.text);dirty=1;}
        else if(e->type==SDL_KEYDOWN&&!e->key.repeat){uint64_t button=0;
            switch(e->key.keysym.sym){
                case SDLK_RETURN:case SDLK_KP_ENTER:button=1ull<<10;break;
                case SDLK_ESCAPE:button=2;break;case SDLK_BACKSPACE:button=4;break;
                case SDLK_LEFT:button=1ull<<12;break;case SDLK_UP:button=1ull<<13;break;
                case SDLK_RIGHT:button=1ull<<14;break;case SDLK_DOWN:button=1ull<<15;break;
                default:break;
            }
            if(button){dm_kbd_press(&model,button);dirty=1;}
        }else if(e->type==SDL_WINDOWEVENT&&owner&&e->window.windowID==SDL_GetWindowID(owner)){
            if(e->window.event==SDL_WINDOWEVENT_CLOSE)model.result=-1;
            if(e->window.event==SDL_WINDOWEVENT_EXPOSED||e->window.event==SDL_WINDOWEVENT_SIZE_CHANGED)dirty=1;
        }
    }
    pthread_mutex_unlock(&lock);return consumed;
}
static void capture(void){
    const char *root=getenv("DM_FRAME_DIR");char path[4096];
    if(!root||snprintf(path,sizeof path,"%s/keyboard-capture-request",root)>=(int)sizeof path||access(path,F_OK))return;
    if(unlink(path))return;
    int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
    if(!surface)return;
    /* Software renderer keeps its target surface; redraw before reading it. */
    draw();
    if(!SDL_RenderReadPixels(renderer,NULL,surface->format->format,surface->pixels,surface->pitch)&&
       snprintf(path,sizeof path,"%s/keyboard-frame-%u.bmp",root,++capture_id)<(int)sizeof path&&
       !SDL_SaveBMP(surface,path))fprintf(stderr,"LINUX_KEYBOARD_CAPTURE path=%s\n",path);
    SDL_FreeSurface(surface);
}
int dm_linux_keyboard_tick(SDL_Window *game,uint64_t buttons,int32_t lx,int32_t ly,uint32_t now){
    pthread_mutex_lock(&lock);int consumed=active||drain;
    uint64_t navigation=(lx< -16000?1ull<<12:0)|(lx>16000?1ull<<14:0)|
        (ly>16000?1ull<<13:0)|(ly< -16000?1ull<<15:0);
    buttons|=navigation;
    if(active){
        if(!canvas){
            int w=0,h=0;owner=game;
            if(dm_vk_linux_keyboard_size(game,&w,&h)&&w>0&&h>0&&w<=4096&&h<=4096)
                canvas=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
            if(canvas)renderer=SDL_CreateSoftwareRenderer(canvas);
            if(!renderer){fprintf(stderr,"LINUX_KEYBOARD_UI_FAILED %s\n",SDL_GetError());model.result=-1;}
            else {SDL_StartTextInput();dirty=1;fprintf(stderr,"LINUX_KEYBOARD_SAME_WINDOW extent=%d,%d\n",w,h);}
        }
        if(!armed){if(!buttons)armed=1;}
        else {
            uint64_t edge=buttons&~previous,dirs=buttons&0xf000;
            if(dirs&&!(previous&0xf000))repeat_at=now+380;
            else if(dirs&&(int32_t)(now-repeat_at)>=0){edge|=dirs;repeat_at=now+110;}
            if(edge){dm_kbd_press(&model,edge);dirty=1;}
        }
        previous=buttons;
        if(model.result){
            SDL_StopTextInput();
            dm_vk_linux_keyboard_close(game);
            if(renderer)SDL_DestroyRenderer(renderer);renderer=NULL;
            if(canvas)SDL_FreeSurface(canvas);canvas=NULL;owner=NULL;
            active=0;drain=1;pthread_cond_signal(&ready);
        }else if(renderer){if(dirty)draw();capture();}
    }else if(drain&&!buttons){drain=0;previous=0;}
    pthread_mutex_unlock(&lock);return consumed;
}
