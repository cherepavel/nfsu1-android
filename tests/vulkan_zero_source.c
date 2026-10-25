#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_zero_source.h"
int main(void)
{
    const uint32_t factors[] = {0,1,0x304,0x305,0x306,0x307,0x8001,0x8002,0x8003,0x8004};
    const uint32_t operations[] = {0x8006,0x800A,0x800B};
    for (unsigned op=0; op<3; op++)
        for (unsigned f=0; f<10; f++) {
            assert(zero_source_safe(1,0,factors[f],operations[op],0,0x204,1));
            assert(zero_source_safe(1,0,factors[f],operations[op],1,0x207,1));
            assert(!zero_source_safe(1,0,factors[f],operations[op],1,0x204,1));
            for (unsigned stage=0; stage<4; stage++)
                assert(!zero_source_safe(1,0,factors[f],operations[op],0,0x204,5u<<(stage*5)));
        }
    assert(!zero_source_safe(0,0,0x8001,0x8006,0,0x207,1));
    assert(!zero_source_safe(1,1,0x8001,0x8006,0,0x207,1));
    assert(!zero_source_safe(1,0,0x8001,0x8007,0,0x207,1)); /* MIN ignores factors */
    assert(!zero_source_safe(1,0,0x8001,0x8008,0,0x207,1)); /* MAX ignores factors */
    for (uint32_t factor=0x300; factor<=0x303; factor++)
        assert(!zero_source_safe(1,0,factor,0x8006,0,0x207,1)); /* source-dependent */
    assert(!zero_source_safe(1,0,0x308,0x8006,0,0x207,1));
    assert(!zero_source_safe(1,0,0xFFFFFFFFu,0x8006,0,0x207,1));
    puts("Zero source: independent factors/ops, alpha and all clip stages: PASS");
}
