// INPUT: A validated, prepared 90447 Linux main image; a nonexistent output.
// OUTPUT: Conditional classic-only HD call wrappers, validated by the runtime contract.
// POS: Computer-only helper; never edits the source or writes process memory.
#include "native_hd_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
int main(int argc,char **argv){
    if(argc!=3)return 2;
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    fputs("little-endian host required\n",stderr);return 2;
#endif
    FILE *in=fopen(argv[1],"rb");if(!in){perror("input");return 1;}
    if(fseek(in,0,SEEK_END)){fclose(in);return 1;}long length=ftell(in);
    if(length<0||(unsigned long)length>DM_NATIVE_HD_MAX_BYTES-3*DM_NATIVE_HD_PAGE||fseek(in,0,SEEK_SET)){fclose(in);return 1;}
    size_t source=(size_t)length,capacity=source+3*DM_NATIVE_HD_PAGE;uint8_t *data=malloc(capacity);if(!data){fclose(in);return 1;}
    if(fread(data,1,source,in)!=source){fclose(in);free(data);return 1;}fclose(in);
    size_t bytes=0;if(!dm_native_hd_prepare(data,source,capacity,&bytes)){fputs("90447 main/control/site/epilogue/stub guard failed\n",stderr);free(data);return 2;}
    int fd=open(argv[2],O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0){perror("new output");free(data);return 1;}
    size_t written=0;while(written<bytes){ssize_t n=write(fd,data+written,bytes-written);if(n<0&&errno==EINTR)continue;if(n<=0){close(fd);free(data);return 1;}written+=(size_t)n;}
    if(close(fd)){free(data);return 1;}
    uint32_t control=dm_native_hd_control(data,bytes),records=control-DM_NATIVE_HD_TRAILER+32;
    printf("{\"source_bytes\":%zu,\"output_bytes\":%zu,\"control_offset\":%u,\"mask_default\":0,\"sites\":[",source,bytes,control);
    for(unsigned i=0;i<DM_NATIVE_HD_SITES;i++)printf("%s{\"site\":%u,\"target\":%u,\"mask_bit\":%u,\"stub\":%u}",i?",":"",dm_native_hd_sites[i].site,dm_native_hd_sites[i].target,1u<<i,dm_hd_read32(data+records+i*16+4));
    puts("],\"runtime_validator_passed\":true}");free(data);return 0;
}
