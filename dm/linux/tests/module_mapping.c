// INPUT: Owned temporary module fixture, private relocation and RX sealing.
// OUTPUT: Unchanged input, zero-filled partial trailer, and empty-file rejection.
// POS: Loader storage regression; no game resources or GPU required.
#include "../module_mapping.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv) {
    assert(argc==2);
    char path[1024];assert(snprintf(path,sizeof path,"%s/module-XXXXXX",argv[1])<(int)sizeof path);
    int fd=mkstemp(path);assert(fd>=0);assert(!ftruncate(fd,0x8000));
    unsigned char value=0x5a;assert(pwrite(fd,&value,1,1234)==1);
    size_t size=0;unsigned char *image=dm_linux_module_file_map(path,&size);
    assert(image!=MAP_FAILED&&size==0x8000&&image[1234]==0x5a&&image[0x7fff]==0);
    image[1234]=0xa5;assert(pread(fd,&value,1,1234)==1&&value==0x5a);
    assert(!mprotect(image,0x4000,PROT_READ|PROT_EXEC)&&image[1234]==0xa5);
    assert(!munmap(image,size));
    assert(!ftruncate(fd,0x7001));value=0x69;assert(pwrite(fd,&value,1,0x7000)==1);
    image=dm_linux_module_file_map(path,&size);
    assert(image!=MAP_FAILED&&size==0x8000&&image[0x7000]==0x69);
    for(size_t i=0x7001;i<size;i++)assert(image[i]==0);
    image[0x7000]=0x96;assert(pread(fd,&value,1,0x7000)==1&&value==0x69);
    assert(!mprotect(image,0x8000,PROT_READ|PROT_EXEC)&&image[0x7000]==0x96);
    for(size_t i=0x7001;i<size;i++)assert(image[i]==0);
    assert(!munmap(image,size));
    assert(!ftruncate(fd,113));value=0x37;assert(pwrite(fd,&value,1,112)==1);
    image=dm_linux_module_file_map(path,&size);
    assert(image!=MAP_FAILED&&size==0x4000&&image[112]==0x37);
    for(size_t i=113;i<size;i++)assert(image[i]==0);
    assert(!mprotect(image,size,PROT_READ|PROT_EXEC)&&image[112]==0x37);
    assert(!munmap(image,size));
    assert(!ftruncate(fd,0));size=7;errno=0;
    assert(dm_linux_module_file_map(path,&size)==MAP_FAILED&&errno==EINVAL&&size==7);
    assert(!close(fd)&&!unlink(path));
    puts("private module mapping preserves input, RX sealing and zero-filled trailer; empty files rejected");
    return 0;
}
