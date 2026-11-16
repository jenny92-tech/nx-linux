// INPUT: Prepared 90447 main, classic-plan admission and optional PID-scoped mask requests.
// OUTPUT: Dedicated writable non-executable mask page only; executable code remains read-only.
// POS: Opt-in native HD submission experiment, disarmed when classic planning declines.
#ifndef DM_LINUX_NATIVE_HD_H
#define DM_LINUX_NATIVE_HD_H
#include "native_hd_policy.h"
#include "native_hd_gate.h"
#include <stdatomic.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef DM_LINUX_GFX
static struct {
    int configured,admitted,world_ready;
    const char *path;_Atomic uint32_t *word;
    unsigned requested;uint64_t sequence,accepted_batches,rejected_batches;
} linux_native_hd;
static int linux_native_hd_init(void){
    if(linux_native_hd.configured)return linux_native_hd.configured>0;
    const char *path=getenv("DM_LINUX_NATIVE_HD_CONTROL"),*value=getenv("DM_LINUX_NATIVE_HD_MASK");
    if(!path&&!value){linux_native_hd.configured=-1;return 0;}
    unsigned requested=0;
    if(path&&strncmp(path,"/run/",5))die("native HD control requires a /run/ path");
    if(value){char *end;errno=0;unsigned long n=strtoul(value,&end,10);
        if(errno||!*value||*end||n>DM_NATIVE_HD_MASK_MAX)die("native HD mask exceeds this build profile");requested=(unsigned)n;}
    module_t *m=NULL;for(int i=0;i<g_nmod;i++)if(!strcmp(g_mod[i].name,"main")){m=&g_mod[i];break;}
    if(!m||strcmp(m->build_id,DM_NATIVE_HD_BUILD)||m->text_end!=DM_NATIVE_HD_TEXT_END||
       m->ro_off!=DM_NATIVE_HD_RO||m->data_off!=DM_NATIVE_HD_DATA||m->patch_off!=DM_NATIVE_HD_PATCH||
       m->key_page_off!=DM_NATIVE_HD_KEYS||!dm_native_hd_validate(m->base,m->size))
        die("native HD control requires matching prepare_classic_native.py modules");
    linux_native_hd.path=path;linux_native_hd.requested=requested;
    linux_native_hd.word=(_Atomic uint32_t *)(void *)(m->base+dm_native_hd_control(m->base,m->size));
    atomic_store_explicit(linux_native_hd.word,0,memory_order_release);
    linux_native_hd.configured=1;
    LOG("LINUX_NATIVE_HD_READY build=%s requested_mask=%u active_mask=0 executable_writes=0",DM_NATIVE_HD_GAME_BUILD,requested);
    return 1;
}
static unsigned linux_native_hd_apply(void){
    unsigned active=dm_native_hd_gated_mask(linux_native_hd.requested,linux_native_hd.admitted,linux_native_hd.world_ready);
    if(atomic_load_explicit(linux_native_hd.word,memory_order_relaxed)!=active)
        atomic_store_explicit(linux_native_hd.word,active,memory_order_release);
    return active;
}
void dm_linux_native_hd_admission(int admitted){
    if(!linux_native_hd_init())return;
    linux_native_hd.admitted=admitted!=0;
    if(admitted)linux_native_hd.accepted_batches++;else linux_native_hd.rejected_batches++;
    linux_native_hd_apply();
}
void dm_linux_native_hd_world_frame(int rendered,uint64_t frame){
    if(!linux_native_hd_init())return;
    int ready=rendered!=0,changed=ready!=linux_native_hd.world_ready;
    linux_native_hd.world_ready=ready;
    unsigned active=linux_native_hd_apply();
    if(changed)LOG("LINUX_NATIVE_HD_WORLD frame=%llu world_ready=%d active_mask=%u",
        (unsigned long long)frame,ready,active);
}
void dm_linux_native_hd_poll(uint64_t frame){
    if(!linux_native_hd_init()||!linux_native_hd.path)return;
    int fd=open(linux_native_hd.path,O_RDONLY|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW);if(fd<0)return;
    struct stat st;char text[128]={0},tail;ssize_t n=-1;
    if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_size>0&&st.st_size<(off_t)sizeof text)
        n=read(fd,text,sizeof text-1);
    close(fd);unsigned long long pid,sequence;unsigned mask;
    if(n<=0||memchr(text,0,(size_t)n)||sscanf(text,"%llu %llu %u %c",&pid,&sequence,&mask,&tail)!=3||
       pid!=(unsigned long long)getpid()||!sequence||sequence<=linux_native_hd.sequence||mask>DM_NATIVE_HD_MASK_MAX)return;
    linux_native_hd.sequence=sequence;linux_native_hd.requested=mask;unsigned active=linux_native_hd_apply();
    LOG("LINUX_NATIVE_HD_CONTROL sequence=%llu after_frame=%llu mask=%u active_mask=%u",sequence,(unsigned long long)frame,mask,active);
}
void dm_linux_native_hd_profile(uint64_t frame){
    if(!linux_native_hd_init())return;
    LOG("LINUX_NATIVE_HD_PROFILE frame=%llu requested_mask=%u active_mask=%u admitted_batches=%llu declined_batches=%llu world_ready=%d",
        (unsigned long long)frame,linux_native_hd.requested,atomic_load(linux_native_hd.word),
        (unsigned long long)linux_native_hd.accepted_batches,(unsigned long long)linux_native_hd.rejected_batches,linux_native_hd.world_ready);
    linux_native_hd.accepted_batches=linux_native_hd.rejected_batches=0;
}
#endif
#endif
