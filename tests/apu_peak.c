#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <fenv.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apu_peak.h"
/* Compile the production selector with a property stub on both hosts. */
#define __ANDROID__ 1
#define PROP_VALUE_MAX 92
#define NFS_LOG_INFO(...) ((void)0)
static unsigned property_reads;
static int __system_property_get(const char *name,char *out)
{
    assert(!strcmp(name,"debug.nfsu.apu_peak_opt")); property_reads++;
    const char *v=getenv("APUPEAK_TEST_PROPERTY");
    return snprintf(out,PROP_VALUE_MAX,"%s",v?v:"");
}
#include "peak_selector.h"
static uint32_t rng=0x12345678;
static uint32_t next(void){uint32_t x=rng;x^=x<<13;x^=x>>17;x^=x<<5;return rng=x;}
static float bits(uint32_t u){float f;memcpy(&f,&u,4);return f;}
static uint32_t word(float f){uint32_t u;memcpy(&u,&f,4);return u;}
static float reference(const float s[][APU_PEAK_ROW_SAMPLES],size_t rows,float peak)
{
    for(size_t r=0;r<rows;r++)for(size_t i=0;i<APU_PEAK_ROW_SAMPLES;i++) {
        float a=fabsf(s[r][i]);if(a>peak)peak=a;
    }
    return peak;
}
static unsigned cases;
static void compare(unsigned trial)
{
    const size_t lengths[]={0,1,2,3,4,7,8,31,32,33,63,64};
    const uint32_t special[]={0,0x80000000,0x7f800000,0xff800000,
        0x7fc01234,0xffc01234,0x7f801234,0xff801234,1,0x80000001,
        0x007fffff,0x807fffff,0x00800000,0x80800000,0xbf800000};
    size_t rows=lengths[trial%12],n=rows*APU_PEAK_ROW_SAMPLES,offset=trial%4;
    float *base=n?malloc((n+offset)*sizeof(float)):NULL; assert(!n||base);
    float (*s)[APU_PEAK_ROW_SAMPLES]=n?(void*)(base+offset):NULL;
    for(size_t r=0;r<rows;r++)for(size_t i=0;i<APU_PEAK_ROW_SAMPLES;i++)
        s[r][i]=bits(trial%3?next():special[next()%15]);
    float seed=bits(trial%2?next():special[(trial/2)%15]);
    float old=reference(s,rows,seed),now=apu_peak_abs_rows(s,rows,seed);
    if(word(old)!=word(now)) {
        fprintf(stderr,"case=%u count=%zu seed=%08x old=%08x new=%08x\n",
            trial,n,word(seed),word(old),word(now));abort();
    }
    free(base);cases++;
}
int main(int argc,char **argv)
{
    if(argc==3&&!strcmp(argv[1],"--selector")) {
        int expected=atoi(argv[2]);assert(apu_peak_optimized()==expected);
        unsigned reads=property_reads;
        assert(!setenv("RECOMP_APU_PEAK_OPT",expected?"0":"1",1));
        assert(apu_peak_optimized()==expected&&reads==property_reads);return 0;
    }
    assert(argc==1);
    const int rounds[]={FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};
    for(unsigned m=0;m<4;m++) {
        assert(!fesetround(rounds[m]));for(unsigned i=0;i<10000;i++)compare(i);
    }
    assert(!fesetround(FE_TONEAREST));
#if defined(__aarch64__)
    uint64_t saved;__asm__ __volatile__("mrs %0,fpcr":"=r"(saved));
    printf("Native default FPCR=%llx\n",(unsigned long long)saved);
    for(unsigned dn=0;dn<2;dn++)for(unsigned fz=0;fz<2;fz++) {
        uint64_t fpcr=(saved&~((1ull<<24)|(1ull<<25)))|((uint64_t)fz<<24)|((uint64_t)dn<<25);
        __asm__ __volatile__("msr fpcr,%0\n\tisb"::"r"(fpcr):"memory");
        for(unsigned i=0;i<10000;i++)compare(i);
    }
    __asm__ __volatile__("msr fpcr,%0\n\tisb"::"r"(saved):"memory");
#endif
    printf("PASS: %u peak scans, exact result bits, NaN seeds/samples, signed zero, subnormals, lengths/alignment and FP controls; neon=%d\n",cases,APU_PEAK_HAS_NEON);
}
