/* Exercises production retirement/cache code with a fake driver. No GPU or
 * guest code: fences/publication are driven explicitly and must precede free. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "../../gfx/nvn/nvn_vk_texture_lifetime.h"
typedef uintptr_t VkImage;typedef uintptr_t VkImageView;typedef uintptr_t VkDeviceMemory;
enum {SUBMISSION_UNUSED,SUBMISSION_RECORDING,SUBMISSION_INFLIGHT,SUBMISSION_GPU_DONE,SUBMISSION_PUBLISHED,SUBMISSION_RETIRED};
enum {DM_VK_MEMORY_TEXTURE};
typedef struct {NvnTextureAllocationKey allocation_key;VkImage image,before_storage,sample_copy,shadow;
    VkImageView view,before_storage_view,sample_view,shadow_view;VkDeviceMemory memory,before_storage_memory;
    uint64_t allocation_size,before_storage_allocation_size,sample_allocation_size,shadow_allocation_size,cpu_snapshot_size;} VTexture;
typedef struct {int phase,nview,npass;uint64_t epoch;struct {VkImage image;} view_keys[4];struct {VkImageView views[9];} framebuffer_keys[4];unsigned clears;} VkSubmission;
static struct {pthread_mutex_t lock;uintptr_t device;uint64_t present_count,image_retired_live;VkSubmission submission_slot,submission_slot2,submission_slot3;} G={.lock=PTHREAD_MUTEX_INITIALIZER};
static uint64_t available=1000000,freed,drains,destructions;
static void die(const char *message,long long value){fprintf(stderr,"%s %lld\n",message,value);abort();}
static void vkDestroyImageView(uintptr_t d,VkImageView v,void *a){(void)d;(void)v;(void)a;}
static void vkDestroyImage(uintptr_t d,VkImage i,void *a){(void)d;(void)i;(void)a;destructions++;}
static void memory_release(VkDeviceMemory m,uint64_t bytes,unsigned kind){(void)m;(void)kind;freed+=bytes;available+=bytes;}
static void texture_snapshot_set(VTexture *t,void *p,uint64_t n){(void)p;(void)n;t->cpu_snapshot_size=0;}
static uint64_t texture_derived_release(VTexture *t){(void)t;return 0;}
int dm_linux_memory_guard_optional(uint64_t bytes){return bytes<=available;}
static void resource_cache_clear(VkSubmission *s){assert(s->phase!=SUBMISSION_INFLIGHT&&s->phase!=SUBMISSION_RECORDING);s->nview=s->npass=0;s->clears++;}
#include "../../gfx/nvn/nvn_vk_texture_forget.inc"
static void async_drain_locked(void);
#include "../../gfx/nvn/nvn_vk_texture_lifetime.inc"
static void async_drain_locked(void){
    drains++;VkSubmission *slots[]={&G.submission_slot,&G.submission_slot2,&G.submission_slot3};
    for(unsigned i=0;i<3;i++)if(slots[i]->phase==SUBMISSION_INFLIGHT){slots[i]->phase=SUBMISSION_RETIRED;texture_retire_complete(slots[i]);}
}
static VTexture image(uintptr_t identity,uint64_t bytes){
    VTexture t={.image=identity,.view=identity+100,.memory=identity+200,.allocation_size=bytes};
    t.allocation_key=(NvnTextureAllocationKey){.width=640,.height=360,.usage=7,.format=3};return t;
}
static void reset(void){
    assert(!texture_lifetime.pending);texture_cache_trim(1);memset(&texture_lifetime,0,sizeof texture_lifetime);
    memset(&G.submission_slot,0,sizeof G.submission_slot);memset(&G.submission_slot2,0,sizeof G.submission_slot2);memset(&G.submission_slot3,0,sizeof G.submission_slot3);
    freed=drains=destructions=0;available=1000000;G.present_count=0;
}
int main(void){
    setenv("DM_VK_IMAGE_CACHE_MB","0",1);setenv("DM_VK_DEFER_TEXTURE_MB","0",1);texture_lifetime_config();
    assert(!texture_lifetime.cache_limit&&!texture_lifetime.retire_limit);
    texture_lifetime.retire_limit=100;texture_lifetime.cache_limit=100;VTexture t=image(1,32);
    G.submission_slot=(VkSubmission){.phase=SUBMISSION_INFLIGHT,.epoch=7,.nview=1,.view_keys={{1}}};
    G.submission_slot2=(VkSubmission){.phase=SUBMISSION_INFLIGHT,.epoch=19,.npass=1,.framebuffer_keys={{{101}}}};
    G.submission_slot3=(VkSubmission){.phase=SUBMISSION_RETIRED,.epoch=5,.nview=1,.view_keys={{999}}};
    assert(texture_retire_try(&t)&&texture_lifetime.pending==1&&!destructions&&!drains);
    G.submission_slot2.phase=SUBMISSION_RETIRED;texture_retire_complete(&G.submission_slot2);
    assert(texture_lifetime.pending==1&&!destructions&&G.submission_slot2.clears==1&&!G.submission_slot3.clears);
    G.submission_slot.epoch=8;G.submission_slot.phase=SUBMISSION_RETIRED;texture_retire_complete(&G.submission_slot);
    assert(texture_lifetime.pending==1&&!destructions); /* newer epoch is insufficient */
    G.submission_slot.epoch=7;texture_retire_complete(&G.submission_slot);
    assert(!texture_lifetime.pending&&texture_lifetime.count==1&&!destructions&&G.submission_slot.clears==1);
    NvnTextureAllocationKey bad=t.allocation_key;bad.usage^=1;VkImage i;VkDeviceMemory m;VkImageView v;uint64_t bytes;
    assert(!texture_allocation_take(&bad,&i,&m,&v,&bytes));
    assert(texture_allocation_take(&t.allocation_key,&i,&m,&v,&bytes)&&i==1&&bytes==32&&!texture_lifetime.cached_bytes);
    reset();texture_lifetime.retire_limit=64;texture_lifetime.cache_limit=64;available=20;
    G.submission_slot=(VkSubmission){.phase=SUBMISSION_INFLIGHT,.epoch=3};t=image(2,32);
    assert(texture_retire_try(&t)&&texture_lifetime.pending==1);
    texture_allocation_pressure(32);
    assert(drains==1&&!texture_lifetime.pending&&!texture_lifetime.count&&freed==32&&available==52);
    reset();texture_lifetime.retire_limit=31;t=image(3,32);
    assert(!texture_retire_try(&t)&&!texture_lifetime.pending&&!destructions);
    texture_lifetime.retire_limit=100;G.submission_slot.phase=SUBMISSION_RECORDING;
    assert(!texture_retire_try(&t)&&!destructions);G.submission_slot.phase=SUBMISSION_RETIRED;
    texture_lifetime.cache_limit=40;texture_allocation_give(&t);t=image(4,32);texture_allocation_give(&t);
    assert(texture_lifetime.count==1&&texture_lifetime.cached_bytes==32&&freed==32);
    G.present_count=300;assert(texture_cache_trim(0)==32&&!texture_lifetime.count&&freed==64);
    reset();puts("production texture retirement/cache/pressure integration passed");return 0;
}
