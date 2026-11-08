/* Run through movie_yuv_native.py, which extracts the actual lifted function. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define RECOMP_GENERATED_CODE
#include "../xboxrecomp/templates/runtime/recomp_types.h"
#include "../src/movie_yuv_native.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
RECOMP_TLS uint32_t g_ebp, g_seh_ebp;
RECOMP_TLS RecompMmx g_mm0, g_mm1, g_mm2, g_mm3, g_mm4, g_mm5, g_mm6, g_mm7;
volatile int g_gil_contended;
void recomp_preempt(void) { abort(); }
#include MOVIE_YUV_REFERENCE

enum { RAM = 0x4000000, Y = 0x400000, U = 0x410000, V = 0x420000,
       OUT = 0x430000, STACK = 0x500000, TABLE = 0x25CC30 };
static uint8_t *reference_ram, *native_ram;
static uint32_t rng = 0x98674u;
static uint32_t random_word(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}
typedef struct { uint32_t gp[9]; RecompMmx mm[8]; } State;
static State state_get(void)
{
    State s = {{g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp},
        {g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7}};
    return s;
}
static void state_set(State s)
{
    g_eax=s.gp[0]; g_ecx=s.gp[1]; g_edx=s.gp[2]; g_esp=s.gp[3];
    g_ebx=s.gp[4]; g_esi=s.gp[5]; g_edi=s.gp[6]; g_ebp=s.gp[7]; g_seh_ebp=s.gp[8];
    g_mm0=s.mm[0]; g_mm1=s.mm[1]; g_mm2=s.mm[2]; g_mm3=s.mm[3];
    g_mm4=s.mm[4]; g_mm5=s.mm[5]; g_mm6=s.mm[6]; g_mm7=s.mm[7];
}
static void compare(unsigned pairs, unsigned unaligned)
{
    g_xbox_mem_offset = (ptrdiff_t)reference_ram;
    uint32_t output = OUT + unaligned;
    MEM32(STACK) = 0x12345678;
    MEM32(STACK+4)=Y; MEM32(STACK+8)=U; MEM32(STACK+12)=V;
    MEM32(STACK+16)=output; MEM32(STACK+20)=output+pairs*8;
    memset(reference_ram+OUT-8,0xA5,pairs*8+24);
    memset(reference_ram+STACK-32,0x5A,32);
    memcpy(native_ram+TABLE,reference_ram+TABLE,6144);
    memcpy(native_ram+Y,reference_ram+Y,pairs*2);
    memcpy(native_ram+U,reference_ram+U,pairs);
    memcpy(native_ram+V,reference_ram+V,pairs);
    memcpy(native_ram+OUT-8,reference_ram+OUT-8,pairs*8+24);
    memcpy(native_ram+STACK-32,reference_ram+STACK-32,64);
    State initial = {0};
    for (unsigned i=0;i<9;i++) initial.gp[i]=random_word();
    for (unsigned i=0;i<8;i++) initial.mm[i].q=((uint64_t)random_word()<<32)|random_word();
    initial.gp[3]=STACK;
    state_set(initial);
    movie_yuv_reference();
    State expected=state_get();
    g_xbox_mem_offset = (ptrdiff_t)native_ram;
    state_set(initial);
    assert(movie_yuv_try_native());
    State actual=state_get();
    assert(!memcmp(expected.gp,actual.gp,sizeof(expected.gp)));
    assert(!memcmp(expected.mm,actual.mm,sizeof(expected.mm)));
    assert(!memcmp(reference_ram+OUT-8,native_ram+OUT-8,pairs*8+24));
    assert(!memcmp(reference_ram+STACK-32,native_ram+STACK-32,64));
    assert(!memcmp(reference_ram+Y,native_ram+Y,pairs*2));
    assert(!memcmp(reference_ram+U,native_ram+U,pairs));
    assert(!memcmp(reference_ram+V,native_ram+V,pairs));
    assert(!memcmp(reference_ram+TABLE,native_ram+TABLE,6144));
}
static void rejected(uint32_t output, uint32_t end, uint32_t y, uint32_t u, uint32_t v)
{
    g_xbox_mem_offset=(ptrdiff_t)native_ram;
    g_esp=STACK;
    MEM32(STACK+4)=y; MEM32(STACK+8)=u; MEM32(STACK+12)=v;
    MEM32(STACK+16)=output; MEM32(STACK+20)=end;
    State before=state_get();
    uint8_t stack_before[64]; memcpy(stack_before,native_ram+STACK-32,64);
    assert(!movie_yuv_try_native());
    State after=state_get();
    assert(!memcmp(before.gp,after.gp,sizeof(before.gp)));
    assert(!memcmp(before.mm,after.mm,sizeof(before.mm)));
    assert(!memcmp(stack_before,native_ram+STACK-32,64));
}
static void benchmark(void)
{
    g_xbox_mem_offset=(ptrdiff_t)native_ram;
    MEM32(STACK+4)=Y; MEM32(STACK+8)=U; MEM32(STACK+12)=V;
    MEM32(STACK+16)=OUT; MEM32(STACK+20)=OUT+640*4;
    for (unsigned pass=0;pass<3;pass++) {
        clock_t start=clock();
        for (unsigned row=0;row<100000;row++) {
            g_esp=STACK;
            if (pass==1) assert(movie_yuv_try_native());
            else movie_yuv_reference();
        }
        double ms=1000.0*(clock()-start)/CLOCKS_PER_SEC;
        printf("isolated 640-pixel row, 100000 calls: %s %.3f CPU ms\n",
            pass==1 ? "native" : "lifted",ms);
    }
}
int main(int argc, char **argv)
{
    reference_ram=calloc(1,RAM); native_ram=calloc(1,RAM);
    assert(reference_ram && native_ram);
    /* Random full-width table words exercise signed saturation AND PADDW wrap. */
    for (unsigned i=0;i<6144;i++) reference_ram[TABLE+i]=(uint8_t)random_word();
    for (unsigned i=0;i<2048;i++) reference_ram[Y+i]=(uint8_t)i;
    for (unsigned u=0;u<256;u++) for (unsigned v=0;v<256;v++) {
        memset(reference_ram+U,(int)u,128);
        memset(reference_ram+V,(int)v,128);
        compare(128,0); /* All 16,777,216 Y/U/V triples. */
    }
    for (unsigned trial=0;trial<1024;trial++) {
        unsigned pairs=1+random_word()%1024;
        for (unsigned i=0;i<6144;i++) reference_ram[TABLE+i]=(uint8_t)random_word();
        for (unsigned i=0;i<pairs*2;i++) reference_ram[Y+i]=(uint8_t)random_word();
        for (unsigned i=0;i<pairs;i++) {
            reference_ram[U+i]=(uint8_t)random_word();
            reference_ram[V+i]=(uint8_t)random_word();
        }
        compare(pairs,trial%8);
    }
    rejected(OUT,OUT,Y,U,V); rejected(OUT,OUT+7,Y,U,V);
    rejected(OUT,OUT-8,Y,U,V); rejected(OUT,OUT+8,0,U,V);
    rejected(OUT,OUT+8,RAM-1,U,V); rejected(OUT,OUT+8,Y,RAM,V);
    rejected(OUT,OUT+8,Y,U,RAM); rejected(RAM-4,RAM+4,Y,U,V);
    rejected(Y,Y+8,Y,U,V); rejected(U,U+8,Y,U,V); rejected(V,V+8,Y,U,V);
    rejected(TABLE,TABLE+8,Y,U,V); rejected(STACK-16,STACK-8,Y,U,V);
    rejected(OUT,OUT+8,STACK-16,U,V); rejected(OUT,OUT+8,Y,STACK-8,V);
    rejected(OUT,OUT+8,Y,U,STACK-4);
    assert(movie_yuv_span(0x80010000u,8));
    assert(movie_yuv_overlap(0x80400000u,8,Y,8));
    if (argc>1 && !strcmp(argv[1],"--benchmark")) benchmark();
    free(reference_ram); free(native_ram);
    puts("movie YUV native: exhaustive triples, randomized rows, registers, stack and fallbacks passed");
    return 0;
}
