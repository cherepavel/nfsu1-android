/* Android-only standalone test of the production title-specific callback. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define __system_property_get test_property_get
#include "../src/d3d_fence_wait.c"
static RecompSpinWaitHook registered;
static const char *property = "1";
static unsigned waits;
int test_property_get(const char *name, char *value)
{
    assert(!strcmp(name,"debug.nfsu.fence_wait"));
    strcpy(value,property);
    return (int)strlen(value);
}
void xbox_set_spin_wait_hook(RecompSpinWaitHook hook) { registered=hook; }
int nv2a_pb_wait_semaphore(uint32_t va, uint32_t submitted, uint32_t delta)
{
    assert(va==0x123400 && submitted==24 && delta==4);
    waits++;
    return 1;
}
int main(void)
{
    nfsu_d3d_fence_wait_init();
    assert(registered);
    RecompSpinWait poll={0x001D0470,4,19,0x123400,24};
    assert(registered(&poll,0) && !waits);
    assert(registered(&poll,1) && waits==1);
    poll.pc++;
    assert(!registered(&poll,0) && !registered(&poll,1) && waits==1);
    poll.pc--;
    property="0";
    for(unsigned i=0;i<128;i++) registered(&poll,0);
    unsigned off_waits=waits;
    for(unsigned i=0;i<128;i++) assert(!registered(&poll,0) && !registered(&poll,1));
    assert(waits==off_waits);
    property="1";
    for(unsigned i=0;i<128;i++) registered(&poll,0);
    assert(registered(&poll,1) && waits>off_waits);
    puts("D3D fence poll: exact PC/register dispatch and live bypass passed");
    return 0;
}
