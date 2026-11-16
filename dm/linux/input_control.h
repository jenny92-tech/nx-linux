// INPUT: Optional process-local diagnostic commands and real controller state.
// OUTPUT: Expiring test input for this D2R process; never global evdev events.
// POS: Linux diagnostic input; real activity cancels it, normal launches opt out.
#ifndef DM_LINUX_INPUT_CONTROL_H
#define DM_LINUX_INPUT_CONTROL_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

typedef struct {
    uint64_t sequence, buttons;
    int32_t lx,ly,rx,ry;
    uint32_t until,next_poll;
    int active;
} dm_linux_test_input;

static int dm_linux_test_command(dm_linux_test_input *s,const char *line,uint32_t now,int real_activity) {
    uint64_t values[8];int32_t axes[4];const char *at=line;
    for(unsigned i=0;i<8;i++) {
        while(isspace((unsigned char)*at))at++;
        if(!*at)return -1;
        char *end;errno=0;
        if(i>=3&&i<=6) {
            long v=strtol(at,&end,10);
            if(errno || end==at || v< -32767 || v>32767)return -1;
            axes[i-3]=(int32_t)v;
        } else {
            if(*at=='-' || *at=='+')return -1;
            unsigned long long v=strtoull(at,&end,i==2?16:10);
            if(errno || end==at)return -1;
            values[i]=(uint64_t)v;
        }
        if(*end && !isspace((unsigned char)*end))return -1;
        at=end;
    }
    while(isspace((unsigned char)*at))at++;
    uint64_t pid=values[0],sequence=values[1],buttons=values[2],hold=values[7];
    if(*at || pid!=(uint64_t)getpid() || !sequence || sequence<=s->sequence || buttons>65535 || hold>2000)return -1;
    s->sequence=sequence;s->active=0;
    if(real_activity)return 0;
    s->buttons=buttons;s->lx=axes[0];s->ly=axes[1];s->rx=axes[2];s->ry=axes[3];
    s->until=now+hold;s->active=hold!=0;
    return 1;
}

static void dm_linux_test_poll(dm_linux_test_input *s,const char *path,uint32_t now,
                               uint64_t *buttons,int32_t *lx,int32_t *ly,int32_t *rx,int32_t *ry) {
    int real=*buttons || abs(*lx)>8192 || abs(*ly)>8192 || abs(*rx)>8192 || abs(*ry)>8192;
    if(real)s->active=0;
    if((int32_t)(now-s->next_poll)>=0) {
        s->next_poll=now+50;
        int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
        if(fd>=0) {
            struct stat st;
            if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<256&&st.st_uid==getuid()) {
                char line[256];ssize_t size=read(fd,line,sizeof line-1);
                if(size>0){line[size]=0;int result=dm_linux_test_command(s,line,now,real);
                    if(result>=0)fprintf(stderr,"LINUX_TEST_INPUT sequence=%llu accepted=%d process_only=1 hold_until=%u\n",
                        (unsigned long long)s->sequence,result,s->until);}
            }
            close(fd);
        }
    }
    if(s->active&&(int32_t)(now-s->until)>=0)s->active=0;
    if(s->active){*buttons=s->buttons;*lx=s->lx;*ly=s->ly;*rx=s->rx;*ry=s->ry;}
}
#endif
