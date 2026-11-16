// INPUT: Controller edges, UTF-8 edits, quantity limits and original name rules.
// OUTPUT: Assertions for accept/cancel, text boundaries and codepoint deletion.
// POS: Host regression for the Linux applet model; no game or SDL required.
#include "keyboard_model.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    DmLinuxKeyboard s;dm_kbd_init(&s,"",15,1,-1);
    assert(!dm_kbd_accept(&s));dm_kbd_press(&s,1);assert(!strcmp(s.text,"A"));
    dm_kbd_press(&s,1ull<<14);dm_kbd_press(&s,1);assert(!strcmp(s.text,"AB"));
    dm_kbd_press(&s,1ull<<11);dm_kbd_press(&s,1);assert(!strcmp(s.text,"ABb"));
    dm_kbd_press(&s,4);assert(!strcmp(s.text,"AB"));
    dm_kbd_press(&s,1ull<<10);assert(s.result==1);
    dm_kbd_init(&s,"",15,1,-1);dm_kbd_append(&s,"-Ab");assert(!dm_kbd_accept(&s));
    dm_kbd_init(&s,"Ab_",15,1,-1);assert(!dm_kbd_accept(&s));
    dm_kbd_append(&s,"Cd");assert(dm_kbd_accept(&s));
    dm_kbd_init(&s,"Ab-C_d",15,1,-1);assert(!dm_kbd_accept(&s));
    dm_kbd_init(&s,"Ab",15,1,-1);dm_kbd_append(&s,"12");assert(!strcmp(s.text,"Ab"));
    dm_kbd_append(&s,"Cdefghijklmnop");assert(!strcmp(s.text,"Ab"));
    dm_kbd_init(&s,"",2,0,-1);dm_kbd_append(&s,"\xf0\x9f\x98\x80");assert(strlen(s.text)==4);
    dm_kbd_append(&s,"A");assert(strlen(s.text)==4);dm_kbd_backspace(&s);assert(!*s.text);
    dm_kbd_append(&s,"\xe4\xb8\xad");dm_kbd_append(&s,"A");assert(dm_kbd_units(s.text)==2);
    dm_kbd_backspace(&s);dm_kbd_backspace(&s);assert(!*s.text);
    assert(dm_kbd_units("\xc0\x80")==-1&&dm_kbd_units("\xed\xa0\x80")==-1);
    assert(dm_kbd_units("\xf4\x90\x80\x80")==-1&&dm_kbd_units("\xe4\xb8")==-1);
    dm_kbd_init(&s,"",10,0,100);dm_kbd_append(&s,"101");assert(!dm_kbd_accept(&s));
    dm_kbd_backspace(&s);assert(dm_kbd_accept(&s));
    dm_kbd_init(&s,"99999999999999999999999999999",0,0,100);assert(!dm_kbd_accept(&s));
    dm_kbd_init(&s,"",15,1,-1);dm_kbd_press(&s,2);assert(s.result==-1&&!s.text[0]);
    dm_kbd_init(&s,"Ab",15,1,-1);s.row=4;s.col=6;dm_kbd_press(&s,1);assert(s.result==1);
    puts("Linux keyboard: controller entry, name validation, UTF-8 boundaries and cancellation passed");
}
