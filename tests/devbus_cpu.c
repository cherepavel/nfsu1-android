#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include "kernel/kernel.h"
/* This fixture uses only memory helpers, not the shim's unused thread adapter. */
#define qemu_thread_create devbus_unused_thread_create
#include "apu/apu_shim.h"
#undef qemu_thread_create
uint8_t *g_apu_ram_ptr;
extern void xbox_ContiguousSetPhysicalRange(uint32_t,uint32_t);
extern HANDLE xbox_irq_line_event(void);
extern uint32_t xbox_irq_lines(void);
extern void xbox_set_irq_line(uint32_t,int);
static unsigned created,closed,signaled;
static char candidate_event,winner_event;
static int inject_race=1;
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES sa,BOOL manual,BOOL initial,LPCSTR name)
{assert(!sa&&!manual&&!initial&&!name);created++;return &candidate_event;}
BOOL CloseHandle(HANDLE h){assert(h==&candidate_event);closed++;return TRUE;}
BOOL SetEvent(HANDLE h){assert(h==&winner_event);signaled++;return TRUE;}
PVOID InterlockedCompareExchangePointer(PVOID volatile *dst,PVOID exchange,PVOID compare)
{
    if(inject_race){*dst=&winner_event;inject_race=0;}
    PVOID previous=compare;__atomic_compare_exchange_n(dst,&previous,exchange,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);return previous;
}
LONG InterlockedOr(volatile LONG *dst,LONG value){return __atomic_fetch_or(dst,value,__ATOMIC_SEQ_CST);}
LONG InterlockedAnd(volatile LONG *dst,LONG value){return __atomic_fetch_and(dst,value,__ATOMIC_SEQ_CST);}
static uint64_t checks;
static void predicate(uint32_t p,uint32_t lo,uint32_t hi)
{assert(xbox_ContiguousIsPhysical(p)==(p>=lo&&p<hi));checks++;}
static void ranges(void)
{
    const uint32_t ranges[][2]={{0,0},{0,1},{0,0x4000000},{0x10000,0x20000},
        {0x3fff000,0x4000000},{0x2000000,0x1000000},{UINT32_MAX-1,UINT32_MAX},{0,UINT32_MAX}};
    for(unsigned r=0;r<sizeof ranges/sizeof*ranges;r++){
        uint32_t lo=ranges[r][0],hi=ranges[r][1];xbox_ContiguousSetPhysicalRange(lo,hi);
        const uint32_t edge[]={0,1,lo-1,lo,lo+1,hi-1,hi,hi+1,0x3ffffff,0x4000000,0x80000000,UINT32_MAX};
        for(unsigned i=0;i<sizeof edge/sizeof*edge;i++)predicate(edge[i],lo,hi);
        /* All 64 MiB byte addresses for representative empty/full/interior ranges. */
        if(r==0||r==2||r==3)for(uint32_t p=0;p<0x4000000;p++)predicate(p,lo,hi);
    }
}
static void mapping(void)
{
    const size_t bytes=(size_t)0x84001000u;
    g_apu_ram_ptr=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(g_apu_ram_ptr!=MAP_FAILED);
    const uint32_t addresses[]={0,4,0xfffc,0x10000,0x10004,0x1fffc,0x20000,0x20004,0x3fffffc};
    const uint32_t ranges[][2]={{0,0},{0,0x4000000},{0x10000,0x20000},{0x20000,0x10000}};
    for(unsigned r=0;r<sizeof ranges/sizeof*ranges;r++){
        uint32_t lo=ranges[r][0],hi=ranges[r][1];xbox_ContiguousSetPhysicalRange(lo,hi);
        for(unsigned i=0;i<sizeof addresses/sizeof*addresses;i++)for(unsigned alias=0;alias<4;alias++){
            uint32_t p=addresses[i];hwaddr address=(hwaddr)p+((uint64_t)alias<<30);
            uint8_t *low=g_apu_ram_ptr+p,*high=g_apu_ram_ptr+0x80000000u+p;
            uint32_t lv=0x12345678u,hv=0x89abcdefu;memcpy(low,&lv,4);memcpy(high,&hv,4);
            int physical=p>=lo&&p<hi;uint8_t *expected=physical?high:low,*other=physical?low:high;
            uint32_t other_word;memcpy(&other_word,other,4);
            assert(apu_phys(address)==expected);
            assert(ldl_le_phys(address_space_memory,address)==(physical?hv:lv));
            assert(lduw_le_phys(address_space_memory,address)==(physical?0xcdef:0x5678));
            assert(ldub_phys(address_space_memory,address)==(physical?0xef:0x78));
            stl_le_phys(address_space_memory,address,0xfedcba98);uint32_t word;memcpy(&word,expected,4);assert(word==0xfedcba98);
            stw_le_phys(address_space_memory,address,0x0123);assert(expected[0]==0x23&&expected[1]==0x01);
            stb_phys(address_space_memory,address,0x42);assert(expected[0]==0x42);
            memcpy(&word,other,4);assert(word==other_word);checks++;
        }
    }
    assert(!munmap(g_apu_ram_ptr,bytes));g_apu_ram_ptr=NULL;
}
static void irq(void)
{
    assert(!xbox_irq_lines());xbox_set_irq_line(32,1);xbox_set_irq_line(UINT32_MAX,1);
    assert(!xbox_irq_lines()&&!created&&!signaled);
    assert(xbox_irq_line_event()==&winner_event&&created==1&&closed==1);
    assert(xbox_irq_line_event()==&winner_event&&created==1);
    uint32_t expected=0;
    for(unsigned i=0;i<32;i++){xbox_set_irq_line(i,1);expected|=1u<<i;assert(xbox_irq_lines()==expected);}
    for(unsigned i=0;i<32;i++)xbox_set_irq_line(i,-1);
    assert(signaled==64&&xbox_irq_lines()==UINT32_MAX);
    for(unsigned i=0;i<32;i++){xbox_set_irq_line(i,0);expected&=~(1u<<i);assert(xbox_irq_lines()==expected);}
    assert(!xbox_irq_lines()&&signaled==64&&created==1&&closed==1);
}
static uint64_t cpu_ns(void){struct timespec t;assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t));return(uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;}
static void benchmark(void)
{
    const unsigned count=16777216;uint32_t sum=0;uint64_t start;
    xbox_ContiguousSetPhysicalRange(0x1000000,0x3000000);
    for(unsigned pattern=0;pattern<3;pattern++){
        for(unsigned i=0;i<1000000;i++)sum+=xbox_ContiguousIsPhysical(i&0x3ffffff);
        sum=0;start=cpu_ns();
        for(unsigned i=0;i<count;i++){
            uint32_t addr=pattern==0?0x8000u:pattern==1?0x1800000u:(i*2654435761u)&0x3ffffffu;
            sum+=xbox_ContiguousIsPhysical(addr);
        }
        printf("pattern=%u ns_call=%.4f checksum=%u\n",pattern,(double)(cpu_ns()-start)/count,sum);
    }
}
int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--bench")){benchmark();return 0;}assert(argc==1);
    ranges();mapping();irq();printf("PASS: %llu address/mapping checks, range boundaries, physical/VA aliases, reads/writes and IRQ/event behavior\n",(unsigned long long)checks);
    return 0;
}
