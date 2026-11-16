// INPUT: UTF-8 text and edge-triggered local keyboard/controller actions.
// OUTPUT: Bounded editable text, original name rules and explicit accept/cancel.
// POS: Pure Linux software-keyboard model; no SDL, guest memory or device I/O.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

enum { DM_KBD_CAP=3007,DM_KBD_ROWS=5,DM_KBD_COLS=10 };
typedef struct {
    char text[DM_KBD_CAP],error[96];
    int max_units,name,numeric_max,row,col,lower,result;
} DmLinuxKeyboard;
static inline int dm_kbd_units(const char *s){
    int units=0;
    while(*s){
        const unsigned char *p=(const unsigned char *)s;unsigned c=*p;int n;
        if(c<0x80)n=1;
        else if(c>=0xc2&&c<=0xdf){n=2;c&=31;}
        else if(c>=0xe0&&c<=0xef){n=3;c&=15;}
        else if(c>=0xf0&&c<=0xf4){n=4;c&=7;}
        else return -1;
        for(int i=1;i<n;i++){if(!p[i]||(p[i]&0xc0)!=0x80)return -1;c=(c<<6)|(p[i]&63);}
        if((n==2&&c<0x80)||(n==3&&c<0x800)||(n==4&&c<0x10000)||
           (c>=0xd800&&c<=0xdfff)||c>0x10ffff||c<32||c==127)return -1;
        units+=c>=0x10000?2:1;s+=n;
    }
    return units;
}
static inline void dm_kbd_init(DmLinuxKeyboard *s,const char *initial,int max_units,int name,int numeric_max){
    memset(s,0,sizeof *s);s->max_units=max_units;s->name=name;s->numeric_max=numeric_max;
    if(initial&&strlen(initial)<sizeof s->text&&dm_kbd_units(initial)>=0)
        memcpy(s->text,initial,strlen(initial)+1);
}
static inline void dm_kbd_append(DmLinuxKeyboard *s,const char *addition){
    size_t old=strlen(s->text),add=strlen(addition);int a=dm_kbd_units(addition),b=dm_kbd_units(s->text);
    if(a<0||b<0||old+add>=sizeof s->text||
       (s->max_units>0&&a+b>s->max_units)||(s->name&&old+add>15)){
        snprintf(s->error,sizeof s->error,"TEXT LIMIT REACHED");return;
    }
    if(s->name)for(size_t i=0;i<add;i++){
        unsigned char c=(unsigned char)addition[i];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='-'||c=='_')){
            snprintf(s->error,sizeof s->error,"NAME: LETTERS, ONE - OR _");return;
        }
    }
    if(s->numeric_max>=0)for(size_t i=0;i<add;i++)if(addition[i]<'0'||addition[i]>'9'){
        snprintf(s->error,sizeof s->error,"DIGITS ONLY");return;
    }
    memcpy(s->text+old,addition,add+1);s->error[0]=0;
}
static inline void dm_kbd_backspace(DmLinuxKeyboard *s){
    size_t n=strlen(s->text);if(n){do{n--;}while(n&&((unsigned char)s->text[n]&0xc0)==0x80);s->text[n]=0;}
    s->error[0]=0;
}
static inline int dm_kbd_accept(DmLinuxKeyboard *s){
    size_t n=strlen(s->text);
    if(s->name){
        int separators=0;
        for(size_t i=0;i<n;i++)separators+=s->text[i]=='-'||s->text[i]=='_';
        if(n<2||n>15||separators>1||s->text[0]=='-'||s->text[0]=='_'||
           (n&&(s->text[n-1]=='-'||s->text[n-1]=='_'))){
            snprintf(s->error,sizeof s->error,"2-15 LETTERS; NO - OR _ AT ENDS");return 0;
        }
        for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)s->text[i];
            if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='-'||c=='_')){
                snprintf(s->error,sizeof s->error,"NAME: LETTERS, ONE - OR _");return 0;}}
    }
    if(s->numeric_max>=0){
        uint64_t value=0;if(!n){snprintf(s->error,sizeof s->error,"ENTER A NUMBER");return 0;}
        for(size_t i=0;i<n;i++){
            unsigned char c=(unsigned char)s->text[i];
            if(c<'0'||c>'9'||value>(uint64_t)s->numeric_max/10||
               value*10+(c-'0')>(uint64_t)s->numeric_max){
                snprintf(s->error,sizeof s->error,"MAXIMUM %d",s->numeric_max);return 0;}
            value=value*10+(c-'0');
        }
    }
    s->result=1;return 1;
}
static inline char dm_kbd_key(const DmLinuxKeyboard *s){
    static const char rows[4][11]={"ABCDEFGHIJ","KLMNOPQRST","UVWXYZ-_ .","0123456789"};
    char c=rows[s->row<4?s->row:0][s->col];
    return s->lower&&c>='A'&&c<='Z'?(char)(c+('a'-'A')):c;
}
static inline void dm_kbd_press(DmLinuxKeyboard *s,uint64_t edge){
    if(edge&2){s->result=-1;return;}
    if(edge&(1ull<<10)){dm_kbd_accept(s);return;}
    if(edge&4)dm_kbd_backspace(s);
    if(edge&(8|1ull<<11))s->lower=!s->lower;
    if(edge&(1ull<<12))s->col=(s->col+DM_KBD_COLS-1)%DM_KBD_COLS;
    if(edge&(1ull<<14))s->col=(s->col+1)%DM_KBD_COLS;
    if(edge&(1ull<<13))s->row=(s->row+DM_KBD_ROWS-1)%DM_KBD_ROWS;
    if(edge&(1ull<<15))s->row=(s->row+1)%DM_KBD_ROWS;
    if(edge&1){
        if(s->row<4){char c[]={dm_kbd_key(s),0};dm_kbd_append(s,c);}
        else switch(s->col/2){
            case 0:dm_kbd_backspace(s);break;
            case 1:s->lower=!s->lower;break;
            case 2:dm_kbd_append(s," ");break;
            case 3:dm_kbd_accept(s);break;
            case 4:s->result=-1;break;
        }
    }
}
