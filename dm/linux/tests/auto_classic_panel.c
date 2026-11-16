// INPUT: Synthetic 90447 panel snapshots, incomplete reads and concurrent changes.
// OUTPUT: No automatic chord in menus/loading, unknown memory or mismatched HUDs.
// POS: Host input-routing regression; no proprietary files or input devices.
#include "auto_classic_panel.h"
#include <assert.h>
#include <stdio.h>
static unsigned char memory[8192];static int changing,reads;
static int read_panel(void *user,uintptr_t address,void *out,size_t bytes){
    (void)user;if(address>sizeof memory||bytes>sizeof memory-address)return 0;
    memcpy(out,memory+address,bytes);
    if(changing&&address==256+136&&++reads==2)((DmPanelList *)out)->count++;
    return 1;
}
static void setup(const char *name,int visible){
    memset(memory,0,sizeof memory);changing=reads=0;
    DmPanelList list={512,1};memcpy(memory+256+88,&list,sizeof list);
    uint64_t panel=1024,header[]={4096,2048};memcpy(memory+512,&panel,8);
    memcpy(memory+1024,header,sizeof header);strcpy((char *)memory+2048,name);memory[1024+81]=(unsigned char)visible;
}
int main(void){
    setup("HUDPanel",1);assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==1);
    setup("HUDPanel",0);assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==0);
    setup("MainMenuPanel",1);assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==2);
    changing=1;assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==-1);
    setup("MainMenuLoadingPanel",1);assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==0);
    setup("UnknownPanel",1);assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==0);
    setup("HUDPanel",1);assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4097)==-1);
    setup("HUDPanel",2);assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==-1);
    setup("HUDPanel",1);DmPanelList oversized={512,257};memcpy(memory+256+88,&oversized,sizeof oversized);
    assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==-1);
    setup("HUDPanel",1);uint64_t bad=8190;memcpy(memory+1024+8,&bad,8);
    assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==-1);
    setup("HUDPanel",1);changing=1;assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==-1);
    setup("HUDPanel",1);DmPanelList two={512,2};memcpy(memory+256+88,&two,sizeof two);
    uint64_t panel=1152,header[]={4096,2176};memcpy(memory+520,&panel,8);memcpy(memory+1152,header,sizeof header);
    strcpy((char *)memory+2176,"SettingsPanel");memory[1152+81]=1;
    assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==0);
    strcpy((char *)memory+2176,"MainMenuPanel");
    assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==0); // HUD still visible.
    memory[1024+81]=0;
    assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==2);
    strcpy((char *)memory+2048,"SettingsPanel");memory[1024+81]=1;
    assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==0); // Modal beats rearm.
    assert(dm_auto_classic_menu_scene(read_panel,NULL,256,4096)==3); // Main+settings may retain 3D.
    memory[1152+81]=0;
    assert(dm_auto_classic_menu_scene(read_panel,NULL,256,4096)==3); // Needs a preceding menu snapshot.
    strcpy((char *)memory+2176,"HUDPanel");memory[1152+81]=1;
    assert(dm_auto_classic_menu_scene(read_panel,NULL,256,4096)==0);
    strcpy((char *)memory+2176,"CinematicsPanel");
    assert(dm_auto_classic_menu_scene(read_panel,NULL,256,4096)==0);
    setup("CharacterSelectPanel",1);
    assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==0);
    assert(dm_auto_classic_menu_scene(read_panel,NULL,256,4096)==4);
    strcpy((char *)memory+2048,"CharacterCreatePanel");
    assert(dm_auto_classic_gameplay_ui(read_panel,NULL,256,4096)==0);
    assert(dm_auto_classic_menu_scene(read_panel,NULL,256,4096)==5);
    memcpy(memory+256+88,&two,sizeof two);memcpy(memory+520,&panel,8);memcpy(memory+1152,header,sizeof header);
    strcpy((char *)memory+2176,"HUDPanel");memory[1152+81]=1;
    assert(dm_auto_classic_menu_scene(read_panel,NULL,256,4096)==0);
    strcpy((char *)memory+2176,"MainMenuLoadingPanel");
    assert(dm_auto_classic_menu_scene(read_panel,NULL,256,4096)==0);
    setup("MainMenuPanel",1);memory[256+216]=1;memory[1024+80]=1;
    assert(dm_auto_classic_exit_main(read_panel,NULL,256));
    changing=1;assert(!dm_auto_classic_exit_main(read_panel,NULL,256));
    setup("MainMenuPanel",1);memory[256+216]=1;memory[1024+80]=1;
    memcpy(memory+256+88,&two,sizeof two);memcpy(memory+520,&panel,8);memcpy(memory+1152,header,sizeof header);memory[1152+81]=1;
    strcpy((char *)memory+2176,"OnlinePlayModeModal");assert(!dm_auto_classic_exit_main(read_panel,NULL,256));
    strcpy((char *)memory+2176,"DeleteCharacterModal");assert(!dm_auto_classic_exit_main(read_panel,NULL,256));
    strcpy((char *)memory+2176,"MainMenuLoadingPanel");assert(!dm_auto_classic_exit_main(read_panel,NULL,256));
    strcpy((char *)memory+2176,"HUDPanel");assert(!dm_auto_classic_exit_main(read_panel,NULL,256));
    strcpy((char *)memory+2176,"TooltipsPanel");assert(dm_auto_classic_exit_main(read_panel,NULL,256));
    memory[256+216]=0;assert(!dm_auto_classic_exit_main(read_panel,NULL,256));
    memory[256+216]=1;memory[1024+80]=0;assert(!dm_auto_classic_exit_main(read_panel,NULL,256));
    puts("auto classic panels: routing and strict exit root, modal/unknown/loading/HUD guards passed");
}
