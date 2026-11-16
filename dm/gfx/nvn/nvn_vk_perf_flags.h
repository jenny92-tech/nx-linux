// INPUT: Two unsigned decimal tokens from a bounded private control file.
// OUTPUT: Exact 64-bit sequence and 32-bit flags; signs/overflow/trailing tokens fail.
// POS: Pure parser shared with host regression tests; no renderer state changes.
#pragma once
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
static inline int nvn_vk_perf_flags_parse(const char *text,unsigned long long *sequence,unsigned *flags){
    const char *p=text;char *end;
    if(!p||!sequence||!flags)return 0;
    while(isspace((unsigned char)*p))p++;
    if(*p<'0'||*p>'9')return 0;
    errno=0;unsigned long long seq=strtoull(p,&end,10);
    if(errno||end==p||!isspace((unsigned char)*end))return 0;
    p=end;while(isspace((unsigned char)*p))p++;
    if(*p<'0'||*p>'9')return 0;
    errno=0;unsigned long long value=strtoull(p,&end,10);
    if(errno||end==p||value>UINT32_MAX)return 0;
    while(isspace((unsigned char)*end))end++;
    if(*end)return 0;
    *sequence=seq;*flags=(unsigned)value;return 1;
}
