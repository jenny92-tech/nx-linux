// INPUT: D2R PID and NVN, command-replay or guarded 90447 matrix entry addresses.
// OUTPUT: Call arguments or selected buffer addresses only; never their data.
// POS: Linux hardware-breakpoint perf probe; no code/stack-data/memory writes.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {int fd,kind,register_count;void *map;size_t bytes;uint64_t samples,lost,skipped;} Event;
static volatile sig_atomic_t stopped;
static void stop(int n){(void)n;stopped=1;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static uint64_t value(const char *s){char *end;errno=0;uint64_t v=strtoull(s,&end,0);return errno||!*s||*end||*s=='-'?0:v;}
static void copy_ring(const uint8_t *data,size_t bytes,uint64_t at,void *out,size_t n){
    size_t off=at%bytes,first=bytes-off;if(first>n)first=n;memcpy(out,data+off,first);if(first<n)memcpy((uint8_t *)out+first,data,n-first);
}
static int drain(Event *e){
    struct perf_event_mmap_page *p=e->map;uint64_t head=__atomic_load_n(&p->data_head,__ATOMIC_ACQUIRE),tail=p->data_tail;
    size_t bytes=p->data_size;if(!bytes||p->data_offset>e->bytes||bytes>e->bytes-p->data_offset||head-tail>bytes)return 0;
    const uint8_t *data=(const uint8_t *)p+p->data_offset;
    while(head-tail>=8){
        struct perf_event_header h;copy_ring(data,bytes,tail,&h,sizeof h);
        if(h.size<8||h.size>head-tail||h.size>bytes)return 0;
        uint8_t row[1200];size_t take=h.size<sizeof row?h.size:sizeof row;copy_ring(data,bytes,tail,row,take);
        if(h.type==PERF_RECORD_SAMPLE){
            if(h.size<48||h.size>sizeof row)return 0;
            uint64_t ip,time,nr,abi,args[7];uint32_t pid,tid;
            memcpy(&ip,row+8,8);memcpy(&pid,row+16,4);memcpy(&tid,row+20,4);memcpy(&time,row+24,8);memcpy(&nr,row+32,8);
            if(nr>128||h.size<48+nr*8)return 0;memcpy(&abi,row+40+nr*8,8);
            if(abi!=PERF_SAMPLE_REGS_ABI_64)e->skipped++;
            else {
                memset(args,0,sizeof args);
                if(h.size<48+nr*8+(size_t)e->register_count*8)return 0;
                memcpy(args,row+48+nr*8,(size_t)e->register_count*8);
                uint64_t lr=args[e->register_count-1];
                if(e->register_count==4)args[3]=args[4]=args[5]=0;
                if(e->register_count==2)args[1]=args[2]=args[3]=args[4]=args[5]=0;
                if(e->register_count==3)args[2]=args[3]=args[4]=args[5]=0;
                printf("%d,%u,%u,%llu,0x%llx,0x%llx",e->kind,pid,tid,(unsigned long long)time,(unsigned long long)ip,(unsigned long long)lr);
                for(unsigned i=0;i<6;i++)printf(",0x%llx",(unsigned long long)args[i]);
                fputs(",\"",stdout);int first=1;
                for(uint64_t i=0;i<nr;i++){uint64_t pc;memcpy(&pc,row+40+i*8,8);if(pc<(1ull<<63)){printf("%s0x%llx",first?"":";",(unsigned long long)pc);first=0;}}
                puts("\"");e->samples++;
            }
        }else if(h.type==PERF_RECORD_LOST){if(h.size<24)return 0;uint64_t lost;memcpy(&lost,row+16,8);e->lost+=lost;}
        tail+=h.size;
    }
    __atomic_store_n(&p->data_tail,tail,__ATOMIC_RELEASE);return tail==head;
}
static int executable_addresses(int pid,const uint64_t address[5],unsigned count,int guest){
    char path[128],exe[1400],line[2048];snprintf(path,sizeof path,"/proc/%d/exe",pid);ssize_t n=readlink(path,exe,sizeof exe-1);
    if(n<0)return 0;exe[n]=0;snprintf(path,sizeof path,"/proc/%d/maps",pid);FILE *in=fopen(path,"r");if(!in)return 0;
    unsigned found=0;while(fgets(line,sizeof line,in)){
        unsigned long long a,b,offset;char permission[5];int at=0;
        if(sscanf(line,"%llx-%llx %4s %llx %*s %*s %n",&a,&b,permission,&offset,&at)!=4||!at||!strchr(permission,'x'))continue;
        char *name=line+at;name[strcspn(name,"\n")]=0;if(!guest&&strcmp(name,exe))continue;
        for(unsigned i=0;i<count;i++)if(address[i]>=a&&address[i]<b)found|=1u<<i;
    }
    fclose(in);return found==(1u<<count)-1;
}
static int matrix_entry(int pid,uint64_t address){
    /* Exact entry of the reviewed 90447 affine matrix multiply. These are
     * executable instruction bytes only, never matrix values or stack data. */
    const uint32_t expected[]={0x6f00e400,0x4ea01c10,0x91008008,0xad400821,
        0xad401043,0x4f811070,0x4ea01c11,0x4f821071};
    uint32_t actual[8];struct iovec local={actual,sizeof actual},remote={(void *)(uintptr_t)address,sizeof actual};
    return process_vm_readv(pid,&local,1,&remote,1,0)==sizeof actual&&!memcmp(actual,expected,sizeof actual);
}
static int render_task_entry(int pid,uint64_t address){
    /* Reviewed 90447 virtual task dispatch, ending at BLR x8. Registers only:
     * x0 task, x1 arguments, x8 target; no task object is dereferenced. */
    const uint32_t expected[]={0x52803108,0x9b084eb8,0xf9400288,0xf9400908,
        0x9104e301,0xaa1403e0,0x52800036,0xd63f0100};
    uint32_t actual[8];struct iovec local={actual,sizeof actual},remote={(void *)(uintptr_t)(address-28),sizeof actual};
    return process_vm_readv(pid,&local,1,&remote,1,0)==sizeof actual&&!memcmp(actual,expected,sizeof actual);
}
int main(int argc,char **argv){
    int matrix=argc==5&&!strcmp(argv[1],"--matrix-90447"),command=argc==5&&!strcmp(argv[1],"--command-pool"),tasks_mode=(argc==5||argc==6)&&!strcmp(argv[1],"--render-tasks-90447"),sync_flow=argc==9&&!strcmp(argv[1],"--sync-flow");
    int tasks_pair=tasks_mode&&argc==6&&!strcmp(argv[5],"--paired");unsigned kinds=tasks_pair?2u:matrix||command||tasks_mode?1u:5u;
    if(tasks_mode&&argc==6&&!tasks_pair)return 2;
    if(!matrix&&!command&&!sync_flow&&!tasks_mode&&argc!=8){fprintf(stderr,"usage: nvn-record-probe PID seconds SetRT DrawArrays DrawArraysInstanced DrawIndexedInstanced DrawIndexedBaseVertex\n       nvn-record-probe --matrix-90447 PID seconds MAIN_BASE\n       nvn-record-probe --command-pool PID seconds COMMAND_REPLAY_ENTRY\n       nvn-record-probe --sync-flow PID seconds Acquire Wait QueueFence WaitRetFast WaitRetOther\n       nvn-record-probe --render-tasks-90447 PID seconds MAIN_BASE\n");return 2;}
    uint64_t pv=value(argv[matrix||command||sync_flow||tasks_mode?2:1]),seconds=value(argv[matrix||command||sync_flow||tasks_mode?3:2]),address[5]={0};
    if(!pv||pv>1u<<30||seconds<1||seconds>15)return 2;int pid=(int)pv;
    if(matrix){uint64_t base=value(argv[4]);if(!base||base>UINT64_MAX-0x107a800u||seconds>5)return 2;address[0]=base+0x107a7c0u;}
    else if(tasks_mode){uint64_t base=value(argv[4]);if(!base||base>UINT64_MAX-0x147a560u||seconds>5)return 2;address[0]=base+0x147a55cu;address[1]=base+0x147a560u;}
    else if(command){if(seconds>5)return 2;address[0]=value(argv[4]);}
    else if(sync_flow){if(seconds>5)return 2;for(unsigned i=0;i<5;i++)address[i]=value(argv[i+4]);}
    else for(unsigned i=0;i<5;i++)address[i]=value(argv[i+3]);
    for(unsigned i=0;i<kinds;i++)if(address[i]<65536||address[i]%4)return 2;
    char path[128],comm[64];snprintf(path,sizeof path,"/proc/%d/comm",pid);FILE *in=fopen(path,"r");
    if(!in||!fgets(comm,sizeof comm,in)){if(in)fclose(in);return 2;}fclose(in);comm[strcspn(comm,"\n")]=0;
    if((strcmp(comm,"d2r-linux")&&strcmp(comm,"d2r-linux-novid"))||!executable_addresses(pid,address,kinds,matrix||tasks_mode)||(matrix&&!matrix_entry(pid,address[0]))||(tasks_mode&&!render_task_entry(pid,address[0]))){
        fprintf(stderr,"PID/addresses do not identify this D2R executable\n");return 2;
    }
    snprintf(path,sizeof path,"/proc/%d/task",pid);DIR *tasks=opendir(path);if(!tasks)return 2;
    Event events[1024]={0};unsigned n=0,threads=0;int result=1;long page=sysconf(_SC_PAGESIZE);struct dirent *entry;
    if(page<=0){closedir(tasks);return 1;}
    while((entry=readdir(tasks))){
        uint64_t tv=value(entry->d_name);if(!tv||tv>1u<<30)continue;threads++;
        for(unsigned k=0;k<kinds;k++){
            if(n==1024){fprintf(stderr,"event capacity exceeded\n");goto done;}
            struct perf_event_attr a={.size=sizeof a,.type=PERF_TYPE_BREAKPOINT,.bp_type=HW_BREAKPOINT_X,.bp_addr=address[k],
                .bp_len=HW_BREAKPOINT_LEN_4,.sample_period=matrix?64:1,.disabled=1,.exclude_kernel=1,.exclude_hv=1,
                .sample_type=PERF_SAMPLE_IP|PERF_SAMPLE_TID|PERF_SAMPLE_TIME|PERF_SAMPLE_CALLCHAIN|PERF_SAMPLE_REGS_USER,
                .sample_regs_user=(matrix?7ull:tasks_mode?0x103ull:command?1ull:sync_flow?3ull:0x3full)|(1ull<<30),.sample_max_stack=32,.exclude_callchain_kernel=1,.wakeup_events=1,
                .use_clockid=1,.clockid=CLOCK_MONOTONIC};
            int fd=(int)syscall(__NR_perf_event_open,&a,(int)tv,-1,-1,PERF_FLAG_FD_CLOEXEC);
            if(fd<0){if(errno==ESRCH)break;fprintf(stderr,"hardware perf tid=%llu kind=%u errno=%d (%s); no kernel settings changed\n",(unsigned long long)tv,k,errno,strerror(errno));goto done;}
            size_t bytes=(size_t)page*33;void *map=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
            if(map==MAP_FAILED){close(fd);perror("perf mmap");goto done;}events[n++]=(Event){.fd=fd,.kind=matrix?5:command?6:tasks_mode?12+(int)k:sync_flow?7+(int)k:(int)k,.register_count=matrix||tasks_mode?4:command?2:sync_flow?3:7,.map=map,.bytes=bytes};
        }
    }
    if(!n)goto done;signal(SIGINT,stop);signal(SIGTERM,stop);
    puts("kind,pid,tid,time_ns,ip,lr,x0,x1,x2,x3,x4,x5,callchain");
    for(unsigned i=0;i<n;i++)if(ioctl(events[i].fd,PERF_EVENT_IOC_ENABLE,0)){perror("perf enable");goto done;}
    double start=now();
    while(!stopped&&now()-start<seconds){usleep(5000);for(unsigned i=0;i<n;i++)if(!drain(&events[i])){fprintf(stderr,"invalid perf ring\n");goto done;}}
    result=0;
done:
    closedir(tasks);uint64_t samples=0,lost=0,skipped=0;
    for(unsigned i=0;i<n;i++){ioctl(events[i].fd,PERF_EVENT_IOC_DISABLE,0);if(!drain(&events[i]))result=1;samples+=events[i].samples;lost+=events[i].lost;skipped+=events[i].skipped;munmap(events[i].map,events[i].bytes);close(events[i].fd);}
    fprintf(stderr,"{\"pid\":%d,\"threads\":%u,\"events\":%u,\"samples\":%llu,\"lost\":%llu,\"skipped\":%llu,\"complete\":%s,\"matrix_90447_mode\":%s,\"command_pool_mode\":%s,\"sync_flow_mode\":%s,\"render_task_mode\":%s,\"render_task_paired\":%s,\"game_code_changed\":false}\n",pid,threads,n,(unsigned long long)samples,(unsigned long long)lost,(unsigned long long)skipped,result?"false":"true",matrix?"true":"false",command?"true":"false",sync_flow?"true":"false",tasks_mode?"true":"false",tasks_pair?"true":"false");
    return result;
}
