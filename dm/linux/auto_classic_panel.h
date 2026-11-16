// INPUT: Read-only snapshots of the reviewed 90447 panel registry and HUD type.
// OUTPUT: HUD/main input gates; renderer-only settings/select/create contexts.
// POS: Linux title input policy; unknown or changing memory suppresses the chord.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef int (*DmPanelRead)(void *,uintptr_t,void *,size_t);
typedef struct {uint64_t pointers,count;} DmPanelList;
/* Exit is stricter than rendering context: an online/delete modal may leave
 * MainMenuPanel visible. Any unrecognized visible root disables host exit. */
static inline int dm_auto_classic_exit_main(DmPanelRead read,void *user,uintptr_t manager){
    static const unsigned offsets[]={136,160,88};
    static const char *const allowed[]={"MainMenuPanel","MainMenuLinks","MainMenuButtonRibbon","FocusManager","TooltipsPanel"};
    if(!read||!manager||(manager&7)||manager>UINTPTR_MAX-217)return 0;
    uint8_t frontend;if(!read(user,manager+216,&frontend,1)||frontend!=1)return 0;
    DmPanelList lists[3],after;int main=0;
    for(unsigned k=0;k<3;k++){
        if(!read(user,manager+offsets[k],&lists[k],sizeof lists[k])||lists[k].count>256||
           (lists[k].count&&(!lists[k].pointers||(lists[k].pointers&7)||lists[k].pointers>UINTPTR_MAX-lists[k].count*8)))return 0;
        uint64_t panels[256];
        if(lists[k].count&&!read(user,lists[k].pointers,panels,(size_t)lists[k].count*8))return 0;
        for(uint64_t i=0;i<lists[k].count;i++){
            uint64_t header[2];uint8_t gates[2];char name[64];uintptr_t panel=panels[i];
            if(!panel||(panel&7)||panel>UINTPTR_MAX-82||!read(user,panel,header,sizeof header)||
               !read(user,panel+80,gates,2)||gates[0]>1||gates[1]>1)return 0;
            if(!gates[1])continue;
            if(!header[1]||header[1]>UINTPTR_MAX-sizeof name||!read(user,header[1],name,sizeof name)||!memchr(name,0,sizeof name))return 0;
            unsigned j=0;while(j<sizeof allowed/sizeof *allowed&&strcmp(name,allowed[j]))j++;
            if(j==sizeof allowed/sizeof *allowed)return 0;
            if(!j){if(!gates[0])return 0;main=1;}
        }
    }
    for(unsigned k=0;k<3;k++)if(!read(user,manager+offsets[k],&after,sizeof after)||memcmp(&after,&lists[k],sizeof after))return 0;
    return read(user,manager+216,&frontend,1)&&frontend==1&&main;
}
static inline int dm_auto_classic_panel_context(DmPanelRead read,void *user,
        uintptr_t manager,uintptr_t hud_vtable,int allow_settings){
    static const unsigned offsets[]={136,160,88};
    static const char *const blocked[]={"MainMenuPanel","MainMenuLoadingPanel",
        "SettingsPanel","CharacterCreatePanel","CharacterSelectPanel",
        "EndScreenPanel","EndScreenPanelExpansion","CinematicsPanel","CreditsPanel"};
    DmPanelList lists[3],after;int hud=0,main_menu=0,modal=0,settings=0,select=0,create=0;
    if(!read||!manager||(manager&7)||manager>UINTPTR_MAX-184||!hud_vtable)return -1;
    for(unsigned k=0;k<3;k++){
        if(!read(user,manager+offsets[k],&lists[k],sizeof lists[k])||lists[k].count>256||
           (lists[k].count&&(!lists[k].pointers||(lists[k].pointers&7)||
            lists[k].pointers>UINTPTR_MAX-lists[k].count*8)))return -1;
        uint64_t panels[256];
        if(lists[k].count&&!read(user,lists[k].pointers,panels,(size_t)lists[k].count*8))return -1;
        for(uint64_t i=0;i<lists[k].count;i++){
            uint64_t header[2];char name[64];uint8_t visible;
            uintptr_t panel=panels[i];
            if(!panel||(panel&7)||panel>UINTPTR_MAX-82||
               !read(user,panel,header,sizeof header)||!header[1]||header[1]>UINTPTR_MAX-sizeof name||
               !read(user,header[1],name,sizeof name)||!memchr(name,0,sizeof name))return -1;
            int is_hud=!strcmp(name,"HUDPanel"),blocking=0;
            for(unsigned j=0;j<sizeof blocked/sizeof *blocked;j++)blocking|=!strcmp(name,blocked[j]);
            if(!is_hud&&!blocking)continue;
            if(!read(user,panel+81,&visible,1)||visible>1)return -1;
            if(blocking&&visible){
                if(!strcmp(name,"MainMenuPanel"))main_menu=1;
                else if(allow_settings&&!strcmp(name,"SettingsPanel"))settings=1;
                else if(allow_settings&&!strcmp(name,"CharacterSelectPanel"))select=1;
                else if(allow_settings&&!strcmp(name,"CharacterCreatePanel"))create=1;
                else modal=1;
            }
            if(is_hud){if(header[0]!=hud_vtable)return -1;hud|=visible;}
        }
    }
    for(unsigned k=0;k<3;k++)
        if(!read(user,manager+offsets[k],&after,sizeof after)||memcmp(&after,&lists[k],sizeof after))return -1;
    if(allow_settings&&!hud&&!modal){
        if(create)return 5;
        if(select)return 4;
        if(settings)return 3;
    }
    if(main_menu&&!hud&&!modal)return 2;
    return main_menu||modal||settings||select||create?0:hud;
}
static inline int dm_auto_classic_gameplay_ui(DmPanelRead read,void *user,
        uintptr_t manager,uintptr_t hud_vtable){
    return dm_auto_classic_panel_context(read,user,manager,hud_vtable,0);
}
/* Settings-only is distinct: a renderer may retain a preceding menu scene,
 * but must never start caching an in-game settings page from this value. */
static inline int dm_auto_classic_menu_scene(DmPanelRead read,void *user,
        uintptr_t manager,uintptr_t hud_vtable){
    return dm_auto_classic_panel_context(read,user,manager,hud_vtable,1);
}
