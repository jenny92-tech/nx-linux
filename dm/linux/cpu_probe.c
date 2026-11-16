// INPUT: Explicit running D2R PID, duration and CPU sampling frequency.
// OUTPUT: PID/TID/user instruction, link-register, call-chain samples and loss counts.
// POS: Read-only Linux perf probe; no stack bytes, game hooks or clock changes.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct { int fd;void *map;size_t bytes;uint64_t samples,lost; } Event;
static volatile sig_atomic_t stopped;
static void stop(int signal_number){(void)signal_number;stopped=1;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int number(const char *text,int low,int high){char *end;errno=0;long n=strtol(text,&end,10);return errno||!*text||*end||n<low||n>high?-1:(int)n;}
static void ring_copy(const uint8_t *data,size_t bytes,uint64_t at,void *out,size_t count){
    size_t offset=at%bytes,first=bytes-offset;if(first>count)first=count;
    memcpy(out,data+offset,first);if(first<count)memcpy((uint8_t *)out+first,data,count-first);
}
static int drain(Event *event){
    struct perf_event_mmap_page *p=event->map;
    uint64_t head=__atomic_load_n(&p->data_head,__ATOMIC_ACQUIRE),tail=p->data_tail;
    size_t bytes=p->data_size;const uint8_t *data=(const uint8_t *)p+p->data_offset;
    if(!bytes||p->data_offset>event->bytes||bytes>event->bytes-p->data_offset||head-tail>bytes)return 0;
    while(head-tail>=sizeof(struct perf_event_header)){
        struct perf_event_header h;ring_copy(data,bytes,tail,&h,sizeof h);
        if(h.size<sizeof h||h.size>bytes||h.size>head-tail)return 0;
        uint8_t row[1200]={0};size_t take=h.size<sizeof row?h.size:sizeof row;ring_copy(data,bytes,tail,row,take);
        if(h.type==PERF_RECORD_SAMPLE){
            if(h.size<40)return 0;
            uint64_t ip,abi,lr=0,nr;uint32_t pid,tid;
            memcpy(&ip,row+8,8);memcpy(&pid,row+16,4);memcpy(&tid,row+20,4);memcpy(&nr,row+24,8);
            if(nr>128||h.size>sizeof row||h.size<40+nr*8)return 0;
            memcpy(&abi,row+32+nr*8,8);
            if(abi){if(h.size<48+nr*8)return 0;memcpy(&lr,row+40+nr*8,8);}
            printf("%u,%u,0x%llx,0x%llx,\"",pid,tid,(unsigned long long)ip,(unsigned long long)lr);
            for(uint64_t i=0;i<nr;i++){uint64_t pc;memcpy(&pc,row+32+i*8,8);if(pc<(1ull<<63))printf("%s0x%llx",i?";":"",(unsigned long long)pc);}
            puts("\"");event->samples++;
        }else if(h.type==PERF_RECORD_LOST){if(h.size<24)return 0;uint64_t lost;memcpy(&lost,row+16,8);event->lost+=lost;}
        tail+=h.size;
    }
    __atomic_store_n(&p->data_tail,tail,__ATOMIC_RELEASE);return tail==head;
}
int main(int argc,char **argv){
    int pid=argc>1?number(argv[1],1,1<<30):-1,seconds=argc>2?number(argv[2],1,60):15,hz=argc>3?number(argv[3],1,1000):99;
    if(argc<2||argc>4||pid<0||seconds<0||hz<0){fprintf(stderr,"usage: cpu-probe D2R_PID [seconds 1..60] [Hz 1..1000]\n");return 2;}
    char path[128],comm[64]={0};snprintf(path,sizeof path,"/proc/%d/comm",pid);FILE *in=fopen(path,"r");
    if(!in||!fgets(comm,sizeof comm,in)){if(in)fclose(in);return 2;}fclose(in);comm[strcspn(comm,"\n")]=0;
    if(strcmp(comm,"d2r-linux")&&strcmp(comm,"d2r-linux-novid")&&strcmp(comm,"d2r-linux-gles-")){fprintf(stderr,"target is not the requested D2R runtime\n");return 2;}
    snprintf(path,sizeof path,"/proc/%d/task",pid);DIR *tasks=opendir(path);if(!tasks)return 2;
    Event events[256]={0};unsigned count=0;int result=1;long page=sysconf(_SC_PAGESIZE);struct dirent *entry;
    if(page<=0){closedir(tasks);return 1;}
    while((entry=readdir(tasks))){
        int tid=number(entry->d_name,1,1<<30);if(tid<0)continue;
        if(count==256){fprintf(stderr,"thread capacity exceeded\n");goto done;}
        struct perf_event_attr attr={.size=sizeof attr,.type=PERF_TYPE_SOFTWARE,.config=PERF_COUNT_SW_CPU_CLOCK,
            .sample_period=1000000000ull/(unsigned)hz,.sample_type=PERF_SAMPLE_IP|PERF_SAMPLE_TID|PERF_SAMPLE_CALLCHAIN|PERF_SAMPLE_REGS_USER,
            .sample_regs_user=1ull<<30,.sample_max_stack=32,.disabled=1,.exclude_kernel=1,.exclude_hv=1,
            .exclude_callchain_kernel=1,.wakeup_events=1};
        int fd=(int)syscall(__NR_perf_event_open,&attr,tid,-1,-1,PERF_FLAG_FD_CLOEXEC);
        if(fd<0){if(errno==ESRCH)continue;fprintf(stderr,"perf_event_open tid=%d errno=%d (%s); no kernel settings changed\n",tid,errno,strerror(errno));goto done;}
        size_t bytes=(size_t)page*9;void *map=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
        if(map==MAP_FAILED){close(fd);perror("perf mmap");goto done;}events[count++]=(Event){.fd=fd,.map=map,.bytes=bytes};
    }
    if(!count)goto done;
    signal(SIGINT,stop);signal(SIGTERM,stop);puts("pid,tid,ip,lr,callchain");
    for(unsigned i=0;i<count;i++)if(ioctl(events[i].fd,PERF_EVENT_IOC_ENABLE,0)){perror("perf enable");goto done;}
    double start=now();
    while(!stopped&&now()-start<seconds){
        usleep(20000);for(unsigned i=0;i<count;i++)if(!drain(&events[i])){fprintf(stderr,"malformed/lost perf ring; sample rejected\n");goto done;}
    }
    result=0;
done:
    closedir(tasks);uint64_t samples=0,lost=0;
    for(unsigned i=0;i<count;i++){
        ioctl(events[i].fd,PERF_EVENT_IOC_DISABLE,0);if(!drain(&events[i]))result=1;
        samples+=events[i].samples;lost+=events[i].lost;munmap(events[i].map,events[i].bytes);close(events[i].fd);
    }
    fprintf(stderr,"{\"pid\":%d,\"threads\":%u,\"samples\":%llu,\"lost\":%llu,\"complete\":%s,\"stack_bytes_read\":0}\n",
        pid,count,(unsigned long long)samples,(unsigned long long)lost,result?"false":"true");
    return result;
}
