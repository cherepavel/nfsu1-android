#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define RECOMP_GENERATED_CODE
#include "../xboxrecomp/templates/runtime/recomp_types.h"
#include "../src/matrix_native.h"
#undef eax
#undef ecx
#undef edx
#undef esp
#undef ebx
#undef esi
#undef edi

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3, g_xmm4, g_xmm5, g_xmm6, g_xmm7;
volatile int g_gil_contended;
void recomp_preempt(void) { abort(); }
void matrix_reference(void);
enum { RAM = 0x4000000, STACK = 0x20000, A = 0x30000, B = 0x31000, OUT = 0x32000 };
static uint8_t *ram;
static unsigned seed = 12345;
static unsigned word(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
typedef struct { uint32_t gp[7]; RecompXmm xmm[8]; } State;
static State get(void)
{
    State s = {{g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi},
        {g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7}};
    return s;
}
static void put(State s)
{
    g_eax=s.gp[0]; g_ecx=s.gp[1]; g_edx=s.gp[2]; g_esp=s.gp[3];
    g_ebx=s.gp[4]; g_esi=s.gp[5]; g_edi=s.gp[6];
    g_xmm0=s.xmm[0]; g_xmm1=s.xmm[1]; g_xmm2=s.xmm[2]; g_xmm3=s.xmm[3];
    g_xmm4=s.xmm[4]; g_xmm5=s.xmm[5]; g_xmm6=s.xmm[6]; g_xmm7=s.xmm[7];
}
static void compare(unsigned dst, unsigned a, unsigned b, int special)
{
    /* Include the complete touched RAM region and canaries. */
    static uint8_t before[0x14000], expected[0x14000];
    for (unsigned i=0;i<sizeof(before)/4;i++) {
        unsigned v=word();
        if (!special) { float f=(int)(v%20001)*0.03125f-312.5f; memcpy(&v,&f,4); }
        memcpy(ram+STACK-64+i*4,&v,4);
    }
    MEM32(STACK+4)=dst; MEM32(STACK+8)=a; MEM32(STACK+12)=b;
    memcpy(before,ram+STACK-64,sizeof(before));
    State initial;
    for(unsigned i=0;i<7;i++) initial.gp[i]=word();
    for(unsigned i=0;i<8;i++) for(unsigned j=0;j<4;j++) initial.xmm[i].u[j]=word();
    initial.gp[3]=STACK;
    put(initial); feclearexcept(FE_ALL_EXCEPT); matrix_reference();
    int exceptions=fetestexcept(FE_ALL_EXCEPT);
    State wanted=get(); memcpy(expected,ram+STACK-64,sizeof(expected));
    memcpy(ram+STACK-64,before,sizeof(before)); put(initial);
    feclearexcept(FE_ALL_EXCEPT);
    if (!matrix_try_native()) {
        assert(special);
        State unchanged=get();
        assert(!memcmp(initial.gp,unchanged.gp,sizeof(initial.gp)));
        assert(!memcmp(initial.xmm,unchanged.xmm,sizeof(initial.xmm)));
        assert(!memcmp(before,ram+STACK-64,sizeof(before)));
        assert(!fetestexcept(FE_ALL_EXCEPT));
        matrix_reference();
    }
    State actual=get();
    assert(exceptions==fetestexcept(FE_ALL_EXCEPT));
    assert(!memcmp(wanted.gp,actual.gp,sizeof(wanted.gp)));
    for(unsigned i=0;i<8;i++) for(unsigned j=0;j<4;j++)
        if(wanted.xmm[i].u[j]!=actual.xmm[i].u[j]) {
            fprintf(stderr,"xmm%u[%u]: %08x != %08x; dst=%x a=%x b=%x special=%d round=%d\n",
                i,j,wanted.xmm[i].u[j],actual.xmm[i].u[j],dst,a,b,special,fegetround());
            abort();
        }
    assert(!memcmp(expected,ram+STACK-64,sizeof(expected)));
}
static void rejected(unsigned stack, unsigned dst, unsigned a, unsigned b)
{
    MEM32(STACK+4)=dst; MEM32(STACK+8)=a; MEM32(STACK+12)=b;
    g_esp=stack; State before=get();
    uint8_t saved[16]; memcpy(saved,ram+STACK,16);
    assert(!matrix_try_native()); State after=get();
    assert(!memcmp(before.gp,after.gp,sizeof(before.gp)));
    assert(!memcmp(before.xmm,after.xmm,sizeof(before.xmm)));
    assert(!memcmp(saved,ram+STACK,16));
}
static void bench(void)
{
    for(unsigned i=0;i<16;i++) {
        float value=(float)(i+1)/16.0f;
        memcpy(ram+A+i*4,&value,4); memcpy(ram+B+i*4,&value,4);
    }
    MEM32(STACK+4)=OUT; MEM32(STACK+8)=A; MEM32(STACK+12)=B;
    for(unsigned k=0;k<6;k++) {
        clock_t start=clock();
        for(unsigned i=0;i<1000000;i++) {
            g_esp=STACK;
            if(k&1) assert(matrix_try_native()); else matrix_reference();
        }
        printf("%s %.3f CPU ms / 1000000 matrices\n",k&1?"native":"scalar",1000.0*(clock()-start)/CLOCKS_PER_SEC);
    }
}
int main(int argc, char **argv)
{
    (void)argv;
    ram=calloc(1,RAM); assert(ram); g_xbox_mem_offset=(ptrdiff_t)ram;
    const unsigned cases[][3]={{OUT,A,B},{A,A,B},{B,A,B},{A,A,A},
        {A+16,A,B},{B-16,A,B},{STACK,STACK,B},{STACK+4,A,STACK},
        {OUT+4,A+4,B+4}};
    const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    unsigned n=0;
    for(unsigned m=0;m<4;m++) {
        assert(!fesetround(modes[m]));
        for(unsigned i=0;i<1000;i++) for(unsigned c=0;c<9;c++) {
            compare(cases[c][0],cases[c][1],cases[c][2],i&1); n++;
        }
    }
    fesetround(FE_TONEAREST);
    rejected(STACK+1,OUT,A,B); rejected(0xFFFFFFFCu,OUT,A,B);
    rejected(STACK,OUT,0xFD000000u,B); rejected(STACK,OUT,A,0x03FFFFD0u);
    rejected(STACK,0x80030000u,A,B);
    printf("PASS: %u matrix cases, all GP/XMM state, RAM/stack alias, raw float bits, FP flags and four rounding modes; invalid/MMIO fallback\n",n);
    if(argc>1) bench();
    free(ram);
}
