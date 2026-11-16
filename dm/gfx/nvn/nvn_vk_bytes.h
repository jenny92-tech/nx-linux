// INPUT: Two readable byte ranges of identical, bounded length.
// OUTPUT: Exact equality across every byte; no hashes, pointer or version shortcuts.
// POS: Boolean comparison microkernel; normal-memory NEON on ARM64, portable fallback.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
static inline int nvn_bytes_equal(const void *a,const void *b,size_t bytes){
    const uint8_t *p=a,*q=b;
#if defined(__aarch64__)
    uint8x16_t x0=vdupq_n_u8(0),x1=x0,x2=x0,x3=x0;
    while(bytes>=64){
        x0=vorrq_u8(x0,veorq_u8(vld1q_u8(p),vld1q_u8(q)));
        x1=vorrq_u8(x1,veorq_u8(vld1q_u8(p+16),vld1q_u8(q+16)));
        x2=vorrq_u8(x2,veorq_u8(vld1q_u8(p+32),vld1q_u8(q+32)));
        x3=vorrq_u8(x3,veorq_u8(vld1q_u8(p+48),vld1q_u8(q+48)));
        p+=64;q+=64;bytes-=64;
    }
    if(vmaxvq_u8(vorrq_u8(vorrq_u8(x0,x1),vorrq_u8(x2,x3))))return 0;
#else
    uint64_t difference=0;
    while(bytes>=8){uint64_t x,y;memcpy(&x,p,8);memcpy(&y,q,8);difference|=x^y;p+=8;q+=8;bytes-=8;}
    if(difference)return 0;
#endif
    unsigned tail=0;while(bytes--){tail|=*p++^*q++;}return tail==0;
}
