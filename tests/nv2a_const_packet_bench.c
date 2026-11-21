/* Link the actual APK's nv2a_pb_exec.c.o, with only private data symbols
 * globalized in a probe copy. Packet code and helpers use the production
 * compiled instructions, with normal relocations for this standalone link.
 * Changes here affect only this standalone process. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "nv2a_const_upload.h"
#define VP_SLOTS 136
#define VP_CONSTS NV2A_VP_CONSTANTS
#include "extern_const_state.h"
extern uint32_t s_reg[0x2000 / 4];
extern int probe_bulk __asm__("pb_const_bulk_enabled.enabled");
extern uint32_t nv2a_pb_exec_run(uint32_t subch, uint32_t method, int noninc,
    const uint32_t *source, uint32_t count);
/* Logging initialization is outside the warmed packet benchmark. */
int nfs_log_enabled(const char *category, int level) { (void)category; (void)level; return 0; }
int nfs_log_message(const char *category, int level, const char *file,
    const char *function, long line, const char *format, ...)
{
    (void)category; (void)level; (void)file; (void)function; (void)line; (void)format; return 0;
}
static uint64_t cpu_ns(void)
{
    struct timespec time; assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time));
    return (uint64_t)time.tv_sec * 1000000000u + (uint64_t)time.tv_nsec;
}
int main(void)
{
    const unsigned sizes[] = {4, 8, 16, 32};
    const unsigned repeats = 3000000;
    for (unsigned size = 0; size < 4; size++) for (unsigned shifted = 0; shifted < 2; shifted++)
        for (unsigned changed = 0; changed < 2; changed++) {
        unsigned slot = shifted ? 15 : 0, words = sizes[size];
        if (slot + words > 32) continue;
        for (unsigned pair = 0; pair < 3; pair++) for (unsigned order = 0; order < 2; order++) {
            probe_bulk = (int)((pair + order) % 2);
            uint32_t input[32];
            for (unsigned i = 0; i < 32; i++) input[i] = i * 0x1234567u + 0x3f800000u;
            s_vp.const_load = 96;
            assert(nv2a_pb_exec_run(0, 0xb80 + slot * 4, 0, input, words) == words);
            unsigned sum = 0;
            uint64_t start = cpu_ns();
            for (unsigned i = 0; i < repeats; i++) {
                s_vp.const_load = 96; input[0] += changed;
                sum += nv2a_pb_exec_run(0, 0xb80 + slot * 4, 0, input, words);
            }
            uint64_t elapsed = cpu_ns() - start;
            assert(sum == repeats * words && s_reg[0xb80 / 4 + slot] == input[0]);
            assert(!memcmp((uint8_t *)s_vp.c + 96 * 16 + (slot % 4) * 4, input, words * 4));
            printf("words=%u slot=%u changed=%u pair=%u bulk=%d cpu_ms=%.3f ns_packet=%.3f checksum=%u\n",
                words, slot, changed, pair, probe_bulk, elapsed / 1e6, (double)elapsed / repeats, sum);
        }
    }
}
