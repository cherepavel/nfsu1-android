#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fenv.h>
#include <time.h>
#define RECOMP_GENERATED_CODE
#include "../xboxrecomp/templates/runtime/recomp_types.h"
#include "../src/culling_native.h"
#undef eax
#undef ecx
#undef edx
#undef esp
#undef esi
#undef ebx
#undef edi
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_esi,g_ebx,g_edi,g_ebp,g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_cc,g_fp_control_word;
volatile int g_gil_contended;
void recomp_preempt(void){abort();}
void sub_00018B40(void){abort();}
void culling_reference(void);
static unsigned rng=12345;
static unsigned word(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static float val(void){return ((int)(word()%20001)-10000)*.03125f;}
enum {STACK=0x20000,A=0x30000,B=0x30100,SELF=0x31000,VIEW=0x32000};
typedef struct {uint32_t gp[9]; double fp[8];int top,cmp;uint16_t cc;} State;
static State get(void){State s={{g_eax,g_ecx,g_edx,g_esp,g_esi,g_ebx,g_edi,g_ebp,g_seh_ebp},{0},g_fp_top,g_fp_cmp,g_fp_cc};memcpy(s.fp,g_fp_stack,sizeof s.fp);return s;}
static void put(State s){g_eax=s.gp[0];g_ecx=s.gp[1];g_edx=s.gp[2];g_esp=s.gp[3];g_esi=s.gp[4];g_ebx=s.gp[5];g_edi=s.gp[6];g_ebp=s.gp[7];g_seh_ebp=s.gp[8];memcpy(g_fp_stack,s.fp,sizeof s.fp);g_fp_top=s.top;g_fp_cmp=s.cmp;g_fp_cc=s.cc;}
static unsigned result_counts[3];
static void compare(unsigned top,unsigned align){
 for(unsigned i=0;i<3;i++){MEMF(A+i*4)=val();MEMF(B+i*4)=val();}
 for(unsigned i=0;i<24;i++)MEMF(VIEW+0xC0+i*4)=val();
 /* Include inside/intersect/outside and boundary equality cases. */
 if((rng&3)==0)for(unsigned i=0;i<24;i++)MEMF(VIEW+0xC0+i*4)=i%4==3?((rng&4)?0.f:1000000.f):0.f;
 MEM32(SELF)=VIEW;uint32_t st=STACK+align*4;
 MEM32(st+4)=A;MEM32(st+8)=B;MEM32(st+12)=0;
 State initial={0};for(unsigned i=0;i<9;i++)initial.gp[i]=word();for(unsigned i=0;i<8;i++)initial.fp[i]=val();initial.gp[1]=SELF;initial.gp[3]=st;initial.top=top;
 unsigned char before[256],expected[256];memcpy(before,(void*)XBOX_PTR(STACK-128),256);
 put(initial);feclearexcept(FE_ALL_EXCEPT);culling_reference();int flags=fetestexcept(FE_ALL_EXCEPT);State want=get();memcpy(expected,(void*)XBOX_PTR(STACK-128),256);
 put(initial);memcpy((void*)XBOX_PTR(STACK-128),before,256);feclearexcept(FE_ALL_EXCEPT);assert(culling_try_native());State actual=get();
 if(memcmp(want.gp,actual.gp,sizeof want.gp)||memcmp(want.fp,actual.fp,sizeof want.fp)||want.top!=actual.top||want.cmp!=actual.cmp||want.cc!=actual.cc||memcmp(expected,(void*)XBOX_PTR(STACK-128),256)||flags!=fetestexcept(FE_ALL_EXCEPT)){
 fprintf(stderr,"mismatch top=%u align=%u result %u/%u flags %d/%d\n",top,align,want.gp[0],actual.gp[0],flags,fetestexcept(FE_ALL_EXCEPT));for(unsigned i=0;i<9;i++)if(want.gp[i]!=actual.gp[i])fprintf(stderr,"gp %u %x %x\n",i,want.gp[i],actual.gp[i]);for(unsigned i=0;i<8;i++)if(memcmp(&want.fp[i],&actual.fp[i],8))fprintf(stderr,"fp %u %.17g %.17g\n",i,want.fp[i],actual.fp[i]);abort();}
 result_counts[g_eax]++;
}
int main(int argc,char **argv){(void)argv;void*ram=calloc(1,0x4000000);assert(ram);g_xbox_mem_offset=(ptrdiff_t)ram;MEMF(0x1F432C)=.5f;MEMF(0x1F4098)=0.f;
 int modes[]={FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};
 for(unsigned m=0;m<4;m++){assert(!fesetround(modes[m]));for(unsigned i=0;i<1000;i++)for(unsigned top=0;top<8;top++)compare(top,i%4);}
 fesetround(FE_TONEAREST);printf("PASS 32000 differential cases, GP/FPU/stack/rounding; results=%u/%u/%u\n",result_counts[0],result_counts[1],result_counts[2]);
 g_esp=STACK;MEM32(STACK+4)=A;MEM32(STACK+8)=B;MEM32(STACK+12)=1;State s=get();assert(!culling_try_native());State t=get();assert(!memcmp(s.gp,t.gp,sizeof s.gp));MEM32(STACK+12)=0;
 /* Unsupported pointers/aliases must retain guest state and scratch RAM. */
 const unsigned bad[]={STACK-32,0xFD000000u,0x03FFFFFCu};
 for(unsigned i=0;i<3;i++){g_ecx=SELF;MEM32(STACK+4)=bad[i];s=get();assert(!culling_try_native());t=get();assert(!memcmp(s.gp,t.gp,sizeof s.gp));assert(!memcmp(s.fp,t.fp,sizeof s.fp));}
 MEM32(STACK+4)=A;MEMF(A)=INFINITY;s=get();assert(!culling_try_native());t=get();assert(!memcmp(s.gp,t.gp,sizeof s.gp));
 MEMF(A)=0.f;
 if(argc>1){
 for(unsigned i=0;i<3;i++){MEMF(A+i*4)=-1.f;MEMF(B+i*4)=1.f;}
 for(unsigned i=0;i<6;i++)for(unsigned j=0;j<4;j++)MEMF(VIEW+0xC0+i*16+j*4)=j==i/2?(i%2?-1.f:1.f):j==3?10.f:0.f;
 if(argc>2) MEMF(VIEW+0xCC)=-10.f;
 g_ecx=SELF;for(unsigned k=0;k<6;k++){clock_t start=clock();for(unsigned i=0;i<1000000;i++){g_ecx=SELF;g_esp=STACK;if(k&1)assert(culling_try_native());else culling_reference();}printf("%s %.3f ms\n",k&1?"native":"reference",1000.*(clock()-start)/CLOCKS_PER_SEC);}}
 free(ram);return 0;}
