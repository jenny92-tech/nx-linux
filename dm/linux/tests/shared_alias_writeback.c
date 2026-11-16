// INPUT: Two real aliases of one memfd, protected guest pages and ownership epochs.
// OUTPUT: Alias writes become visible with dirty notification and no second data copy.
// POS: Linux CPU-memory regression; real GPU fence/DMA ioctl validation remains on device.
#include "../../gfx/nvn/nvn_vk_shared_epoch.h"
#include "../../gfx/nvn/nvn_dirty.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
void crash_install(void);
void dm_guest_write_prepare(void *,size_t);
int main(void){
    crash_install();size_t n=(size_t)sysconf(_SC_PAGESIZE)*8;
    int fd=memfd_create("shared-writeback-test",MFD_CLOEXEC);assert(fd>=0);assert(!ftruncate(fd,(off_t)n));
    unsigned char *guest=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    unsigned char *other=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    assert(guest!=MAP_FAILED&&other!=MAP_FAILED&&guest!=other);
    memset(guest,0x42,n);nvn_dirty_add_range(guest,n);NvnVkSharedEpoch state={0};
    for(unsigned epoch=1;epoch<=100;epoch++){
        uint64_t stamp=nvn_dirty_now();nvn_dirty_protect(guest,n);
        assert(!nvn_dirty_since(guest,n,stamp));
        assert(nvn_vk_shared_epoch_acquire(&state,epoch));assert(nvn_vk_shared_epoch_release(&state,epoch));
        assert(!nvn_vk_shared_alias_writeback_ready(&state,epoch,1,1));
        /* Write through the other alias to model the memory visibility issue,
         * not a GPU completion. Guest virtual-page protection cannot catch it. */
        memset(other,(int)(epoch&255),n);assert(!nvn_dirty_since(guest,n,stamp));
        assert(nvn_vk_shared_epoch_complete(&state,epoch));
        assert(nvn_vk_shared_alias_writeback_ready(&state,epoch,1,1));
        dm_guest_write_prepare(guest,n);assert(nvn_dirty_since(guest,n,stamp));
        for(size_t i=0;i<n;i++)assert(guest[i]==(epoch&255));
        assert(!memcmp(guest,other,n));
        assert(!nvn_vk_shared_alias_writeback_ready(&state,epoch+1,1,1));
        assert(!nvn_vk_shared_alias_writeback_ready(&state,epoch,0,1));
        assert(!nvn_vk_shared_alias_writeback_ready(&state,epoch,1,0));
    }
    nvn_dirty_remove_range(guest);assert(!munmap(guest,n));assert(!munmap(other,n));close(fd);
    puts("shared alias writeback: real backing visibility, 100 dirty notifications and epoch/import/uncached guards passed");
}
