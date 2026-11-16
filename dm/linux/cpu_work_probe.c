// INPUT: Explicit D2R PID, bounded duration and the RK3566 Cortex-A55 PMU.
// OUTPUT: Per-thread user CPU cycles/instructions/stalls/L1 activity with coverage.
// POS: Read-only computation attribution; no code, scheduling or clock changes.
#include <dirent.h>
#include <errno.h>
#include <linux/perf_event.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

enum { EVENTS=6,THREADS=256 };
static const char *names[EVENTS]={"cpu_cycles","inst_retired","stall_backend","stall_frontend","l1d_cache_refill","l1d_cache"};
typedef struct {int tid,fd[EVENTS];char comm[64];} Group;
static int number(const char *s,int max){
    if(!s||*s<'0'||*s>'9')return -1;
    char *end;errno=0;long n=strtol(s,&end,10);
    return errno||*end||n<1||n>max?-1:(int)n;
}
static int read_text(const char *path,char *text,size_t size){
    FILE *f=fopen(path,"r");if(!f)return 0;
    int ok=fgets(text,(int)size,f)!=NULL;fclose(f);
    if(ok)text[strcspn(text,"\n")]=0;
    return ok;
}
int main(int argc,char **argv){
    int pid=argc>1?number(argv[1],1<<30):-1,seconds=argc>2?number(argv[2],60):10;
    if(argc<2||argc>3||pid<0||seconds<0){fprintf(stderr,"usage: cpu-work-probe D2R_PID [seconds 1..60]\n");return 2;}
    char path[256],text[128];snprintf(path,sizeof path,"/proc/%d/comm",pid);
    if(!read_text(path,text,sizeof text)||(strcmp(text,"d2r-linux")&&strcmp(text,"d2r-linux-novid"))){fprintf(stderr,"target is not D2R\n");return 2;}
    const char *pmu="/sys/bus/event_source/devices/armv8_cortex_a55";
    snprintf(path,sizeof path,"%s/type",pmu);
    if(!read_text(path,text,sizeof text))return 1;
    int type=number(text,65535);if(type<0)return 1;
    uint64_t config[EVENTS];
    for(unsigned i=0;i<EVENTS;i++){
        snprintf(path,sizeof path,"%s/events/%s",pmu,names[i]);unsigned event;char tail;
        if(!read_text(path,text,sizeof text)||sscanf(text,"event=%x%c",&event,&tail)!=1)return 1;
        config[i]=event;
    }
    Group group[THREADS];unsigned count=0;int result=1;
    for(unsigned t=0;t<THREADS;t++)for(unsigned e=0;e<EVENTS;e++)group[t].fd[e]=-1;
    snprintf(path,sizeof path,"/proc/%d/task",pid);DIR *tasks=opendir(path);if(!tasks)return 1;
    struct dirent *entry;
    while((entry=readdir(tasks))){
        int tid=number(entry->d_name,1<<30);if(tid<0)continue;
        if(count==THREADS){closedir(tasks);goto done;}
        Group *g=&group[count++];g->tid=tid;
        snprintf(path,sizeof path,"/proc/%d/task/%d/comm",pid,tid);
        if(!read_text(path,g->comm,sizeof g->comm)){closedir(tasks);goto done;}
        for(unsigned e=0;e<EVENTS;e++){
            struct perf_event_attr a={.size=sizeof a,.type=(unsigned)type,.config=config[e],
                .disabled=e==0,.exclude_kernel=1,.exclude_hv=1,
                .read_format=PERF_FORMAT_GROUP|PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING};
            g->fd[e]=(int)syscall(__NR_perf_event_open,&a,tid,-1,e?g->fd[0]:-1,PERF_FLAG_FD_CLOEXEC);
            if(g->fd[e]<0){fprintf(stderr,"PMU event %s tid=%d: %s\n",names[e],tid,strerror(errno));closedir(tasks);goto done;}
        }
    }
    closedir(tasks);if(!count)goto done;
    for(unsigned t=0;t<count;t++)if(ioctl(group[t].fd[0],PERF_EVENT_IOC_ENABLE,PERF_IOC_FLAG_GROUP))goto done;
    struct timespec wait={seconds,0};while(nanosleep(&wait,&wait)&&errno==EINTR){}
    for(unsigned t=0;t<count;t++)if(ioctl(group[t].fd[0],PERF_EVENT_IOC_DISABLE,PERF_IOC_FLAG_GROUP))goto done;
    printf("pid,tid,comm,enabled_ns,running_ns");for(unsigned e=0;e<EVENTS;e++)printf(",%s",names[e]);puts("");
    for(unsigned t=0;t<count;t++){
        uint64_t data[3+EVENTS];Group *g=&group[t];
        if(read(g->fd[0],data,sizeof data)!=(ssize_t)sizeof data||data[0]!=EVENTS||data[2]>data[1])goto done;
        /* comm is owned D2R thread metadata, but still quote CSV correctly. */
        printf("%d,%d,\"",pid,g->tid);for(const char *s=g->comm;*s;s++){if(*s=='"')putchar('"');putchar(*s);}printf("\",%llu,%llu",(unsigned long long)data[1],(unsigned long long)data[2]);
        for(unsigned e=0;e<EVENTS;e++)printf(",%llu",(unsigned long long)data[3+e]);puts("");
    }
    fprintf(stderr,"{\"pid\":%d,\"threads\":%u,\"seconds\":%d,\"complete\":true,\"user_only\":true,\"pmu\":\"armv8_cortex_a55\"}\n",pid,count,seconds);result=0;
done:
    for(unsigned t=0;t<count;t++)for(unsigned e=0;e<EVENTS;e++)if(group[t].fd[e]>=0)close(group[t].fd[e]);
    return result;
}
