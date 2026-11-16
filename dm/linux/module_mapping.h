// INPUT: One prepared native module, including a possibly partial last page.
// OUTPUT: Private writable mapping; relocations cannot change the input file.
// POS: Linux loader storage policy; unchanged code pages remain reclaimable.
#pragma once
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static inline void *dm_linux_module_file_map(const char *path,size_t *size) {
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return MAP_FAILED;
    struct stat st;
    if(fstat(fd,&st)){int error=errno;close(fd);errno=error;return MAP_FAILED;}
    long page=sysconf(_SC_PAGESIZE);
    if(!S_ISREG(st.st_mode)||st.st_size<=0||(uintmax_t)st.st_size>SIZE_MAX-0x3fff||
       page<=0||page>0x4000||(page&(page-1))){
        close(fd);errno=EINVAL;return MAP_FAILED;
    }
    size_t length=((size_t)st.st_size+0x3fff)&~(size_t)0x3fff;
    void *image=mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(image==MAP_FAILED){int error=errno;close(fd);errno=error;return MAP_FAILED;}
    /* Overlay only complete file pages inside our own reservation. The final
     * partial page and 16 KiB rounding remain anonymous zero-fill, matching the
     * old copy exactly without ever exposing a file page beyond EOF. */
    size_t prefix=(size_t)st.st_size&~((size_t)page-1);
    if(prefix&&mmap(image,prefix,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_FIXED,fd,0)==MAP_FAILED)goto failed;
    for(size_t at=prefix;at<(size_t)st.st_size;){
        ssize_t got=pread(fd,(unsigned char *)image+at,(size_t)st.st_size-at,(off_t)at);
        if(got<0&&errno==EINTR)continue;
        if(got<=0){if(!got)errno=EIO;goto failed;}
        at+=(size_t)got;
    }
    close(fd);*size=length;
    return image;
failed:
    {int error=errno;munmap(image,length);close(fd);errno=error;return MAP_FAILED;}
}
