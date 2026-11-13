#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "nv2a_vk_indexed.h"
static uint8_t *ram;
static uint32_t s_palette_reg = 0x1000, s_palette_va = 64;
static ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)ram; }
#ifndef INDEXED_TEST_NON_ANDROID
#define __ANDROID__ 1
#define PROP_VALUE_MAX 92
#define NFS_LOG_INFO(...) ((void)0)
static unsigned property_reads;
static int __system_property_get(const char *name, char *out)
{
    assert(!strcmp(name, "debug.nfsu.indexed_tiles")); property_reads++;
    const char *value = getenv("P8_TEST_PROPERTY");
    return snprintf(out, PROP_VALUE_MAX, "%s", value ? value : "");
}
#else
#undef __ANDROID__
#endif
#include "indexed_swizzle.h"
#include "indexed_decoder.h"
#include "vulkan_indexed_reference.h"
static uint32_t random_state = 0x12345678;
static uint32_t next(void)
{
    uint32_t x = random_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return random_state = x;
}
static unsigned cases;
static size_t pixels_checked;
static uint32_t power2(uint32_t n) { uint32_t p=1; while(p<n)p*=2; return p; }
static void compare(uint32_t w, uint32_t h, unsigned mode, unsigned alignment, int invalid)
{
    size_t pixels = (size_t)w*h, source_bytes = (size_t)power2(w)*power2(h);
    uint32_t va = 4096 + alignment;
    ram = malloc(va + source_bytes); assert(ram);
    s_palette_reg = invalid ? mode : 0x1000 | (mode << 2);
    s_palette_va = 64 + alignment;
    for (size_t i=0; i<va+source_bytes; i++) ram[i] = (uint8_t)next();
    /* Alternate random indices and a ramp that visits every palette index. */
    if (alignment&1) for (size_t i=0; i<source_bytes; i++) ram[va+i] = (uint8_t)i;
    size_t prefix = 32 + (alignment%4)*4, bytes = pixels*4;
    uint8_t *old = malloc(prefix+bytes+1), *now = malloc(prefix+bytes+1); assert(old&&now);
    memset(old,0xa5,prefix+bytes+1); memset(now,0xa5,prefix+bytes+1);
#ifndef INDEXED_TEST_NON_ANDROID
    uint64_t tiled = s_indexed_stats.tiled, scalar = s_indexed_stats.scalar, bad = s_indexed_stats.invalid;
#endif
    int a=legacy_decode_indexed(va,w,h,(uint32_t *)(old+prefix));
    /* Production must rebuild tables itself after a shape switch; it cannot
     * rely on the oracle's preceding use of the shared renderer tables. */
    s_swz_w=s_swz_h=0;
    memset(s_swz_x,0xa5,sizeof s_swz_x); memset(s_swz_y,0xa5,sizeof s_swz_y);
    int b=decode_indexed(va,w,h,(uint32_t *)(now+prefix));
    assert(a==b && !memcmp(old,now,prefix+bytes+1));
#ifndef INDEXED_TEST_NON_ANDROID
    if (indexed_tiles_enabled()) {
        int eligible = NV2A_VK_INDEXED_NEON && w>=8 && h>=8 && w<=512 && h<=512
            && !(w&(w-1)) && !(h&(h-1));
        assert(s_indexed_stats.invalid==bad+(unsigned)invalid);
        assert(s_indexed_stats.tiled==tiled+(unsigned)(!invalid&&eligible));
        assert(s_indexed_stats.scalar==scalar+(unsigned)(!invalid&&!eligible));
    }
#endif
    cases++; pixels_checked+=pixels; free(old); free(now); free(ram); ram=NULL;
}
static uint64_t cpu_ns(void)
{
    struct timespec t; assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t));
    return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;
}
static int (*volatile upload)(uint32_t,uint32_t,uint32_t,uint32_t *);
static void benchmark(void)
{
    const uint32_t dims[][2]={{8,8},{64,64},{128,512},{512,128},{512,512},
        {8,512},{512,8},{4,512},{1024,1024}};
    ram=malloc(4096+8*1024*1024); uint32_t *out=malloc(4*1024*1024); assert(ram&&out);
    for (unsigned i=0;i<4096+8*1024*1024;i++) ram[i]=(uint8_t)next();
    s_palette_va=64;
    for (unsigned d=0;d<sizeof dims/sizeof*dims;d++) for (unsigned sources=1;sources<=8;sources*=8)
        for (unsigned palette=0;palette<=2;palette+=2) {
            uint32_t w=dims[d][0],h=dims[d][1],pixels=w*h,rounds=16777216/pixels;
            s_palette_reg=0x1000|(palette<<2);
            for (unsigned i=0;i<rounds;i++) assert(decode_indexed(4096,w,h,out));
            for (unsigned pair=0;pair<3;pair++) for (unsigned order=0;order<2;order++) {
                unsigned mode=(pair+order)%2; upload=mode?decode_indexed:legacy_decode_indexed;
                uint64_t begin=cpu_ns(); uint32_t sum=0;
                for (unsigned i=0;i<rounds;i++) {
                    assert(upload(4096+(i%sources)*pixels,w,h,out)); sum+=out[(i*43u)%pixels];
                }
                printf("%ux%u sources=%u entries=%u pair=%u tiles=%u ns_pixel=%.4f checksum=%u\n",
                    w,h,sources,256u>>palette,pair,mode,(double)(cpu_ns()-begin)/(rounds*pixels),sum);
            }
        }
    free(ram); free(out); ram=NULL;
}
int main(int argc,char **argv)
{
#ifndef INDEXED_TEST_NON_ANDROID
    if (argc==3&&!strcmp(argv[1],"--selector")) {
        int expected=atoi(argv[2]); assert(indexed_tiles_enabled()==expected);
        unsigned reads=property_reads; assert(!setenv("RECOMP_VK_INDEXED_TILES",expected?"0":"1",1));
        assert(indexed_tiles_enabled()==expected&&property_reads==reads);
        compare(16,16,0,0,0); assert(s_indexed_stats.calls==(unsigned)expected); return 0;
    }
    assert(indexed_tiles_enabled()==1);
#endif
    if(argc==2&&!strcmp(argv[1],"--bench")){benchmark();return 0;}
    assert(argc==1);
    for (unsigned mode=0;mode<4;mode++) for (unsigned alignment=0;alignment<4;alignment++) {
        for (unsigned w=1;w<=4096;w*=2) for (unsigned h=1;h<=4096;h*=2)
            if((size_t)w*h<=65536) compare(w,h,mode,alignment,0);
        const uint32_t other[][2]={{512,512},{1024,1024},{3,3},{6,4},{4,6},{7,9},{17,15},{0,0}};
        for(unsigned i=0;i<sizeof other/sizeof*other;i++) compare(other[i][0],other[i][1],mode,alignment,0);
    }
    for(unsigned flags=0;flags<64;flags++)compare(8,8,flags,flags%4,1);
    /* Rejected helper dimensions must not even read the supplied pointers. */
    uint32_t sentinel=0x12345678;
    const uint32_t rejected[][2]={{0,0},{4,8},{8,4},{513,8},{8,513},{7,8},{8,9},{UINT32_MAX,8}};
    for(unsigned i=0;i<sizeof rejected/sizeof*rejected;i++) {
        assert(!nv2a_vk_indexed_tiles(NULL,rejected[i][0],rejected[i][1],NULL,NULL,NULL,&sentinel));
        assert(sentinel==0x12345678);
    }
    printf("PASS: %u production/legacy images, %zu pixels; palette modes, source/output alignment, exact ends, fallback and invalid palette; neon=%d\n",
        cases,pixels_checked,NV2A_VK_INDEXED_NEON);
    return 0;
}
