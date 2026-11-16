/* INPUT: Synthetic shader fixture, or explicit private SPIR-V/metadata paths.
 * OUTPUT: Guard regressions; optional transformed module for external validation.
 * POS: Host test tool only; never deploys or embeds private game shaders. */
#include "nvn_vk_driver_push.h"
#include <assert.h>
#include <stdio.h>
static const uint32_t fixture[]={0x07230203,0x10300,0,23,0,
  0x20011,1,0x3000e,0,1,0x6000f,4,18,0x6e69616d,0,17,0x30010,18,7,
  0x40047,10,6,16,0x30047,11,2,0x50048,11,0,35,0,
  0x40047,14,34,1,0x40047,14,33,3,0x40047,17,30,0,
  0x20013,1,0x30021,2,1,0x30016,3,32,0x40017,4,3,4,0x40015,5,32,0,
  0x4002b,5,6,0,0x4002b,5,7,4096,0x4002b,5,8,22,0x4002b,5,9,2,
  0x4001c,10,4,7,0x3001e,11,10,0x40020,13,2,11,0x4003b,13,14,2,
  0x40020,15,2,3,0x40020,16,3,4,0x4003b,16,17,3,
  0x50036,1,18,0,2,0x200f8,19,0x70041,15,20,14,6,8,9,
  0x4003d,3,21,20,0x70050,4,22,21,21,21,21,0x3003e,17,22,0x100fd,0x10038};
static void *read_file(const char *path,size_t *size) {
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 if(fseek(f,0,SEEK_END)||ftell(f)<0){fclose(f);return NULL;}
 *size=(size_t)ftell(f);rewind(f);void *p=malloc(*size);
 if(!p||fread(p,1,*size,f)!=*size){free(p);fclose(f);return NULL;}
 fclose(f);return p;
}
int main(int argc,char **argv) {
 uint32_t *out=NULL;size_t nout=0;NvnVkDriverPush plan;
 if(argc==4){
  size_t bytes=0,meta_bytes=0;uint32_t *code=read_file(argv[1],&bytes),*m=read_file(argv[2],&meta_bytes);
  if(!code||!m||bytes%4||meta_bytes<48||m[0]!=0x4d4b5644||m[3]>64||meta_bytes<48+m[3]*32)return 2;
  NvnVkDriverResource r[64]={0};for(unsigned i=0;i<m[3];i++){
   uint32_t *b=m+12+i*8;r[i]=(NvnVkDriverResource){b[0],b[1],b[4],b[3]==8};
  }
  int applied=nvn_vk_driver_push(code,bytes/4,r,m[3],m[2]==5?32:16,m[2]==1?64:0,&out,&nout,&plan);
  FILE *f=fopen(argv[3],"wb");if(!f)return 3;
  const void *data=applied?(void *)out:(void *)code;size_t size=applied?nout*4:bytes;
  if(fwrite(data,1,size,f)!=size||fclose(f))return 4;
  printf("{\"applied\":%d,\"fields\":%u,\"offset\":%u}\n",applied,plan.count,plan.offset);
  free(out);free(code);free(m);return 0;
 }
 assert(argc==1);NvnVkDriverResource resource={1,3,2,1};
 size_t count=sizeof fixture/sizeof *fixture;
 assert(nvn_vk_driver_push(fixture,count,&resource,1,16,64,&out,&nout,&plan));
 assert(plan.count==1&&plan.offset==64&&plan.words[0].bank==2&&plan.words[0].offset==0x168&&plan.bindings==1);
 assert(nout>count&&out[3]>fixture[3]);free(out);
 for(size_t n=0;n<count;n++)assert(!nvn_vk_driver_push(fixture,n,&resource,1,16,64,&out,&nout,&plan));
 uint32_t copy[sizeof fixture/4];memcpy(copy,fixture,sizeof fixture);
 for(size_t at=5;at<count;at+=copy[at]>>16)if(copy[at]==0x40047&&copy[at+1]==10&&copy[at+2]==6){copy[at+3]=32;break;}
 assert(!nvn_vk_driver_push(copy,count,&resource,1,16,64,&out,&nout,&plan));
 memcpy(copy,fixture,sizeof fixture);
 for(size_t at=5;at<count;at+=copy[at]>>16)if(copy[at]==0x4002b&&copy[at+2]==8){copy[at]=0x40032;break;} /* SpecConstant index */
 assert(!nvn_vk_driver_push(copy,count,&resource,1,16,64,&out,&nout,&plan));
 assert(!nvn_vk_driver_push(fixture,count,&resource,1,0,64,&out,&nout,&plan));
 puts("driver push guards: passed");return 0;
}
