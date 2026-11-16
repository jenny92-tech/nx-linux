// INPUT: Renderer identity, monotonic time, game present callbacks and /proc RSS.
// OUTPUT: One readable bitmap line: FPS, active API and current RAM usage.
// POS: Linux scanout HUD; sampling is throttled and needs no font dependency.
#include "status_hud.h"
#include "../host/dm.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    int enabled,started,fps_valid,rss_valid;
    unsigned scale;
    uint64_t fps_sample_ns,ram_sample_ns,frames,rss_bytes;
    double fps;
    uint8_t *draw_target;unsigned draw_width,draw_height,draw_scale;int draw_bgra;
    char api[32],line[160];
} hud;

static void label(char *out,size_t capacity,const char *value){
    if(!value||!*value)value="unknown";
    size_t i=0;
    for(;i+1<capacity&&value[i];i++){
        unsigned char c=(unsigned char)value[i];out[i]=c>=32&&c<127?(char)c:'?';
    }
    out[i]=0;
}
void dm_linux_status_hud_init(const char *api){
    const char *enabled=getenv("DM_LINUX_STATUS_HUD");
    memset(&hud,0,sizeof hud);hud.scale=1;hud.enabled=enabled&&!strcmp(enabled,"1");
    label(hud.api,sizeof hud.api,api);
    if(hud.enabled)fprintf(stderr,"LINUX_STATUS_HUD api=%s ram=process_RSS ram_sample_ms=500 fps_sample_ms=1000\n",hud.api);
}
int dm_linux_status_hud_extent(unsigned output_width,unsigned output_height,unsigned *width,unsigned *height){
    if(!hud.enabled||output_width<16||output_height<12)return 0;
    hud.scale=output_width>=520&&output_height>=24?2u:1u;
    *width=output_width<500?output_width:500;*height=12*hud.scale;
    return 1;
}
static int sample(uint64_t now){
    int changed=0;
    if(!hud.started||now<hud.fps_sample_ns||now-hud.fps_sample_ns>=1000000000){
        uint64_t frames=dm_nvn_frame_count();
        if(hud.started&&now>hud.fps_sample_ns&&frames>=hud.frames){
            hud.fps=(double)(frames-hud.frames)*1e9/(double)(now-hud.fps_sample_ns);hud.fps_valid=1;
        }else hud.fps_valid=0;
        hud.fps_sample_ns=now;hud.frames=frames;changed=1;
    }
    if(!hud.started||now<hud.ram_sample_ns||now-hud.ram_sample_ns>=500000000){
        hud.ram_sample_ns=now;hud.rss_valid=0;changed=1;
        FILE *file=fopen("/proc/self/status","r");
        if(file){
            char line[192];unsigned long long kib;
            while(fgets(line,sizeof line,file))if(sscanf(line,"VmRSS: %llu kB",&kib)==1){
                if(kib<=UINT64_MAX/1024){hud.rss_bytes=(uint64_t)kib*1024;hud.rss_valid=1;}break;
            }
            fclose(file);
        }
    }
    hud.started=1;if(!changed)return 0;
    char fps[24],ram[32];
    if(hud.fps_valid)snprintf(fps,sizeof fps,"%.1f",hud.fps);else strcpy(fps,"--.-");
    if(hud.rss_valid)snprintf(ram,sizeof ram,"%.0f MiB",(double)hud.rss_bytes/1048576);else strcpy(ram,"-- MiB");
    snprintf(hud.line,sizeof hud.line,"FPS %s | %s | RAM %s",fps,hud.api,ram);
    return 1;
}
/* Hand-authored 5x7 row glyphs, matching the controller keyboard's bitmap style. */
static const uint8_t letters[26][7]={
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
static void text(uint8_t *pixels,unsigned width,unsigned height,unsigned y,const char *line,int bgra){
    unsigned scale=hud.scale,x=4*scale;
    for(;*line&&x+5*scale<=width;line++,x+=6*scale){
        unsigned char c=(unsigned char)toupper((unsigned char)*line);const uint8_t *rows=NULL;uint8_t special[7]={0};
        if(c>='A'&&c<='Z')rows=letters[c-'A'];
        else if(c>='0'&&c<='9')rows=digits[c-'0'];
        else {switch(c){
            case '-':special[3]=31;break;case '.':special[6]=4;break;
            case '|':for(int i=0;i<7;i++)special[i]=4;break;
            case '(':special[0]=special[6]=2;for(int i=1;i<6;i++)special[i]=4;break;
            case ')':special[0]=special[6]=8;for(int i=1;i<6;i++)special[i]=4;break;
            case ' ':break;default:special[0]=14;special[1]=17;special[2]=1;special[3]=2;special[4]=4;special[6]=4;break;
        }rows=special;}
        for(unsigned row=0;row<7;row++)for(unsigned col=0;col<5;col++)if(rows[row]&(1u<<(4-col)))
            for(unsigned dy=0;dy<scale;dy++)for(unsigned dx=0;dx<scale;dx++){
                unsigned py=y+row*scale+dy,px=x+col*scale+dx;if(py>=height)continue;
                uint8_t *p=pixels+((size_t)py*width+px)*4;
                p[bgra?2:0]=row==6?220:245;p[1]=row==6?220:245;p[bgra?0:2]=row==6?220:245;p[3]=255;
            }
    }
}
int dm_linux_status_hud_pixels(uint8_t *pixels,unsigned width,unsigned height,int bgra,uint64_t now_ns){
    if(!hud.enabled||!pixels||!width||!height)return 0;
    int changed=sample(now_ns);
    if(!changed&&hud.draw_target==pixels&&hud.draw_width==width&&hud.draw_height==height&&hud.draw_scale==hud.scale&&hud.draw_bgra==bgra)return 0;
    hud.draw_target=pixels;hud.draw_width=width;hud.draw_height=height;hud.draw_scale=hud.scale;hud.draw_bgra=bgra;
    for(size_t i=0,n=(size_t)width*height;i<n;i++){
        pixels[4*i]=16;pixels[4*i+1]=16;pixels[4*i+2]=16;pixels[4*i+3]=255;
    }
    text(pixels,width,height,2*hud.scale,hud.line,bgra);
    return 1;
}
