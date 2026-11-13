#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <fenv.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "filter_scalar.h"
#include "apu_filter_neon.h"
#define __ANDROID__ 1
#define PROP_VALUE_MAX 92
#define NFS_LOG_INFO(...) ((void)0)
static unsigned property_reads __attribute__((unused));
static int __system_property_get(const char *,char *) __attribute__((unused));
static int __system_property_get(const char *name,char *out)
{
    assert(!strcmp(name,"debug.nfsu.apu_filter_neon"));property_reads++;
    const char *v=getenv("APUFILTER_TEST_PROPERTY");
    return snprintf(out,PROP_VALUE_MAX,"%s",v?v:"");
}
#include "filter_selector.h"
static uint32_t rng=0x12345678;
static uint32_t next(void){uint32_t x=rng;x^=x<<13;x^=x>>17;x^=x<<5;return rng=x;}
static float bits(uint32_t u){float f;memcpy(&f,&u,4);return f;}
static void scalar(sv_filter sv[2],float s[][2],unsigned count)
{
    for(unsigned ch=0;ch<2;ch++)for(unsigned i=0;i<count;i++)
        s[i][ch]=fminf(fmaxf(run_svf(&sv[ch],s[i][ch]),-1.0f),1.0f);
}
static void paired(sv_filter sv[2],float s[][2],unsigned count)
{
#if APU_FILTER_HAS_NEON
    if (!apu_filter_lowpass_pair(sv,s,count)) scalar(sv,s,count);
#else
    scalar(sv,s,count);
#endif
}
static void relocate(sv_filter sv[2]){sv[0].op=&sv[0].l;sv[1].op=&sv[1].l;}
static unsigned cases;
static void compare(unsigned trial)
{
    const unsigned counts[]={0,1,2,3,7,31,32,33,64};unsigned count=counts[trial%9];
    sv_filter old[2],now[2];
    for(unsigned ch=0;ch<2;ch++) {
        if(trial%3) {
            memset(&old[ch],0,sizeof old[ch]);
            setup_svf(&old[ch],(next()%256+1)/256.0f,(next()%256+32)/288.0f,F_LP);
            old[ch].b=(int32_t)next()/2147483648.0f;old[ch].l=(int32_t)next()/2147483648.0f;
        } else {
            uint32_t fields[offsetof(sv_filter,op)/sizeof(float)];
            for(unsigned i=0;i<sizeof fields/sizeof *fields;i++)fields[i]=next();
            memcpy(&old[ch],fields,sizeof fields);
        }
    }
    relocate(old);memcpy(now,old,sizeof old);relocate(now);
    float (*a)[2]=count?malloc(count*sizeof *a):NULL;
    float (*b)[2]=count?malloc(count*sizeof *b):NULL;
    assert(!count||(a&&b));
    for(unsigned i=0;i<count;i++)for(unsigned ch=0;ch<2;ch++)
        a[i][ch]=trial%3?(int32_t)next()/1073741824.0f:bits(next());
    if(count)memcpy(b,a,count*sizeof *a);
    scalar(old,a,count);paired(now,b,count);
    if(count)assert(!memcmp(a,b,count*sizeof *a));
    for(unsigned ch=0;ch<2;ch++) {
        if(memcmp(&old[ch],&now[ch],offsetof(sv_filter,op))) {
            fprintf(stderr,"filter state differs: case=%u ch=%u\n",trial,ch);
            for(unsigned j=0;j<offsetof(sv_filter,op)/4;j++) {
                uint32_t x,y;memcpy(&x,(char*)&old[ch]+j*4,4);memcpy(&y,(char*)&now[ch]+j*4,4);
                fprintf(stderr,"%u old=%08x new=%08x\n",j,x,y);
            }
            abort();
        }
        assert(now[ch].op==&now[ch].l);
    }
    free(a);free(b);cases++;
}
static uint64_t cpu_ns(void){struct timespec t;assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t));return(uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;}
static void (*volatile run)(sv_filter[2],float[][2],unsigned);
static void benchmark(void)
{
    sv_filter initial[2];memset(initial,0,sizeof initial);
    setup_svf(&initial[0],0.25f,0.375f,F_LP);setup_svf(&initial[1],0.375f,0.625f,F_LP);
    float input[32][2];for(unsigned i=0;i<32;i++){input[i][0]=sinf((float)i);input[i][1]=cosf((float)i);}
    const unsigned frames=262144;
    for(unsigned pair=0;pair<3;pair++)for(unsigned order=0;order<2;order++) {
        unsigned mode=(pair+order)%2;run=mode?paired:scalar;
        sv_filter sv[2];float data[32][2];uint32_t sum=0;uint64_t start=cpu_ns();
        for(unsigned i=0;i<frames;i++) {
            if(!(i%256)){memcpy(sv,initial,sizeof sv);relocate(sv);}
            memcpy(data,input,sizeof data);run(sv,data,32);
            uint32_t word;memcpy(&word,&data[i%32][i%2],4);sum+=word;
        }
        printf("pair=%u neon=%u ns_stereo_frame=%.3f checksum=%u\n",pair,mode,(double)(cpu_ns()-start)/frames,sum);
    }
}
int main(int argc,char **argv)
{
    if(argc==3&&!strcmp(argv[1],"--selector")) {
        int expected=atoi(argv[2])&&APU_FILTER_HAS_NEON;assert(apu_filter_neon_enabled()==expected);
        unsigned reads=property_reads;assert(!setenv("RECOMP_APU_FILTER_NEON",expected?"0":"1",1));
        assert(apu_filter_neon_enabled()==expected&&property_reads==reads);return 0;
    }
    assert(apu_filter_neon_enabled()==APU_FILTER_HAS_NEON);
    if(argc==2&&!strcmp(argv[1],"--bench")){benchmark();return 0;}
    assert(argc==1);
#if APU_FILTER_HAS_NEON
    /* Rejection must leave all data intact, even when a NaN is at the end. */
    sv_filter sv[2],before[2];memset(sv,0,sizeof sv);
    setup_svf(&sv[0],0.25f,0.5f,F_LP);setup_svf(&sv[1],0.5f,0.25f,F_LP);
    float input[32][2]={{0}},saved_input[32][2];
    assert(apu_filter_lowpass_pair(sv,input,32));
    input[31][1]=bits(0x7f800123u);memcpy(before,sv,sizeof sv);memcpy(saved_input,input,sizeof input);
    assert(!apu_filter_lowpass_pair(sv,input,32));
    assert(!memcmp(sv,before,sizeof sv)&&!memcmp(input,saved_input,sizeof input));
    assert(apu_filter_lowpass_pair(NULL,NULL,0));
#endif
    const int rounds[]={FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};
    for(unsigned m=0;m<4;m++) {
        assert(!fesetround(rounds[m]));for(unsigned i=0;i<5000;i++)compare(i);
    }
    assert(!fesetround(FE_TONEAREST));
#if defined(__aarch64__)
    uint64_t saved;__asm__ __volatile__("mrs %0,fpcr":"=r"(saved));
    for(unsigned dn=0;dn<2;dn++)for(unsigned fz=0;fz<2;fz++) {
        uint64_t fpcr=(saved&~((1ull<<24)|(1ull<<25)))|((uint64_t)fz<<24)|((uint64_t)dn<<25);
        __asm__ __volatile__("msr fpcr,%0\n\tisb"::"r"(fpcr):"memory");
        for(unsigned i=0;i<5000;i++)compare(i);
    }
    __asm__ __volatile__("msr fpcr,%0\n\tisb"::"r"(saved):"memory");
#endif
    printf("PASS: %u scalar/paired filter blocks, all state/sample bits, independent channels, lengths, NaNs/subnormals and native FP controls; neon=%d\n",cases,APU_FILTER_HAS_NEON);
    return 0;
}
