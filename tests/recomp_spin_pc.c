#include <assert.h>
#include <stdio.h>
#define RECOMP_GENERATED_CODE
#include "../xboxrecomp/templates/runtime/recomp_types.h"
static unsigned wakes, yields;
static uint32_t captured[5];
void recomp_spin_wake(void) { wakes++; }
void recomp_spin_yield_at(uint32_t pc, uint32_t ax, uint32_t cx, uint32_t dx, uint32_t si)
{
    captured[0]=pc; captured[1]=ax; captured[2]=cx; captured[3]=dx; captured[4]=si;
    yields++;
}
static void lifted_poll(void)
{
    /* Match the lifter's local shadow registers, whose current values must
     * be supplied instead of reading unrelated TLS globals in the runtime. */
    uint32_t g_eax=4, g_ecx=19, g_edx=0x123400, g_esi=24;
    for(unsigned i=0;i<64;i++) RECOMP_SPIN_HINT_AT(0x001D0470u);
}
int main(void)
{
    lifted_poll();
    assert(wakes==4 && yields==1);
    assert(captured[0]==0x001D0470 && captured[1]==4 && captured[2]==19
        && captured[3]==0x123400 && captured[4]==24);
    for(unsigned i=0;i<64;i++) RECOMP_SPIN_HINT();
    assert(wakes==8 && yields==2);
    for(unsigned i=0;i<5;i++) assert(captured[i]==0);
    puts("spin poll PC and local register snapshots passed");
    return 0;
}
