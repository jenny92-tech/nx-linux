/* Execute the exact A64 hook/stub; independently supplied flags and expected
 * outcomes cover original requests, the one extra size, and unchanged state. */
#include "../aspect_policy.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
extern int aspect_invoke(void *,uint64_t,int,int,int);
__asm__(".text\n.global aspect_invoke\naspect_invoke:\n"
    "stp x20,x21,[sp,#-32]!\nstr x30,[sp,#16]\nmov x16,x0\nmsr nzcv,x1\n"
    "mov w8,w2\nmov w21,w3\nmov w20,w4\nblr x16\n"
    "ldr x30,[sp,#16]\nldp x20,x21,[sp],#32\nret\n");
#define CHECK(x) do{if(!(x)){fprintf(stderr,"ASPECT_POLICY_FAIL line=%d\n",__LINE__);return 1;}}while(0)
int main(void){
    long page=sysconf(_SC_PAGESIZE);CHECK(page>512);uint32_t *p=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(p!=MAP_FAILED);
    CHECK(soul_aspect_branch((uintptr_t)p,(uintptr_t)p+64,p));CHECK(soul_aspect_stub(p+16,(uintptr_t)p+64,(uintptr_t)p+256));
    p[64]=0x1a9f07e0;p[65]=0xd65f03c0; /* cset w0,ne; ret */
    __builtin___clear_cache((char *)p,(char *)p+264);CHECK(!mprotect(p,page,PROT_READ|PROT_EXEC));
    static const struct{uint64_t flags;int changed,w,h,want;}cases[]={
        {0x80000000,1,640,360,1},{0x60000000,1,1280,720,1},
        {0x80000000,0,640,360,0},{0x20000000,1,640,480,1},
        {0x30000000,1,640,480,1},{0x20000000,0,640,480,0},
        {0x20000000,1,640,481,0},{0x20000000,1,641,480,0},
        {0x20000000,1,800,600,0},{0x20000000,1,0,0,0},
        {0x80000000,1,1920,1080,1},{0x20000000,2,640,480,1}
    };
    for(unsigned i=0;i<sizeof cases/sizeof *cases;i++)CHECK(aspect_invoke(p,cases[i].flags,cases[i].changed,cases[i].w,cases[i].h)==cases[i].want);
    static const int modes[][2]={{640,480},{480,360},{320,240}};
    for(unsigned mode=0;mode<3;mode++){
        CHECK(!mprotect(p,page,PROT_READ|PROT_WRITE));
        CHECK(soul_aspect_stub_size(p+16,(uintptr_t)p+64,(uintptr_t)p+256,modes[mode][0],modes[mode][1]));
        __builtin___clear_cache((char *)p,(char *)p+264);CHECK(!mprotect(p,page,PROT_READ|PROT_EXEC));
        CHECK(aspect_invoke(p,0x20000000,1,modes[mode][0],modes[mode][1])==1);
        CHECK(aspect_invoke(p,0x20000000,0,modes[mode][0],modes[mode][1])==0);
        CHECK(aspect_invoke(p,0x20000000,1,modes[mode][0]+1,modes[mode][1])==0);
        CHECK(aspect_invoke(p,0x80000000,1,1920,1080)==1);
    }
    uint32_t b;CHECK(!soul_aspect_branch(0x100000000ull,0x108000000ull,&b));CHECK(soul_aspect_branch(0x108000000ull,0x100000000ull,&b));CHECK(!soul_aspect_branch(4,9,&b));CHECK(!munmap(p,page));
    puts("{\"passed\":true,\"cases\":24,\"actual_a64_hook_and_stub\":true,\"original_predicate_preserved\":true,\"branch_reach_checked\":true}");return 0;
}
