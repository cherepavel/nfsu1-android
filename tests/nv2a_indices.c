#include "../xboxrecomp/src/kernel/nv2a_indices.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    uint32_t src[131];
    uint32_t actual[263], expected[263];
    for (unsigned i = 0; i < 131; ++i)
        src[i] = (0xfedc0000u + i * 0x10001u) ^ (i * 0x1234567u);
    unsigned cases = 0;
    for (uint32_t cap = 0; cap <= 261; ++cap)
        for (uint32_t used = 0; used <= cap; ++used)
            for (uint32_t words = 0; words <= 131; ++words) {
                memset(actual, 0xa5, sizeof actual);
                memset(expected, 0xa5, sizeof expected);
                uint32_t end = used;
                for (uint32_t i = 0; i < words && end + 2 <= cap; ++i) {
                    expected[1 + end++] = (uint16_t)src[i];
                    expected[1 + end++] = (uint16_t)(src[i] >> 16);
                }
                assert(nv2a_append_indices16(actual + 1, used, cap, src, words) == end);
                assert(memcmp(actual, expected, sizeof actual) == 0);
                ++cases;
            }
    printf("PASS: %u boundary/order cases\n", cases);
    return 0;
}
