/* The runner extracts the actual packet routine, selector and VP state.
 * Compare the new path with the retained production scalar path, including
 * untouched state and source bounds. --bench measures packet CPU work only. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define NFS_LOG_INFO(...) ((void)0)
#ifdef NV2A_CONST_PORTABLE
/* Limit this switch to this helper; keep the target ABI and system headers. */
#define NV2A_CONST_NO_NEON 1
#endif
#include "nv2a_const_upload.h"
#include "nv2a_indices.h"
#define VP_SLOTS 136
#define VP_CONSTS NV2A_VP_CONSTANTS
#define NV097_SET_TRANSFORM_CONSTANT 0x0b80u
#define NV097_ARRAY_ELEMENT16 0x1800u
#define NV_MAX_INDICES 64u
#include "const_state_under_test.h"
static uint32_t s_reg[0x2000 / 4];
static uint8_t s_reg_dirty[0x2000 / 64];
static struct { uint32_t prim, idx_count, idx[NV_MAX_INDICES]; } s_gpu;
static int bulk, verbose;
static int pb_const_bulk_enabled(void) { return bulk; }
static int pb_verbose(void) { return verbose; }
#include "const_packet_under_test.h"

#define PROP_VALUE_MAX 92
static unsigned property_reads;
static int test_property_get(const char *name, char *out)
{
    assert(!strcmp(name, "debug.nfsu.const_bulk")); property_reads++;
    const char *value = getenv("PBCONST_TEST_PROPERTY");
    return snprintf(out, PROP_VALUE_MAX, "%s", value ? value : "");
}
#define __system_property_get test_property_get
#define pb_const_bulk_enabled production_selector
#ifndef __ANDROID__
#define __ANDROID__ 1
#define TEST_UNDEF_ANDROID 1
#endif
#include "const_selector_under_test.h"
#ifdef TEST_UNDEF_ANDROID
#undef __ANDROID__
#endif
#undef pb_const_bulk_enabled
#undef __system_property_get

typedef struct {
    __typeof__(s_vp) vp;
    uint32_t reg[0x2000 / 4];
    uint8_t dirty[0x2000 / 64];
    __typeof__(s_gpu) gpu;
} Snapshot;
static Snapshot initial, expected, actual;
static void capture(Snapshot *state)
{
    memset(state, 0, sizeof *state);
    memcpy(&state->vp, &s_vp, sizeof s_vp); memcpy(state->reg, s_reg, sizeof s_reg);
    memcpy(state->dirty, s_reg_dirty, sizeof s_reg_dirty); memcpy(&state->gpu, &s_gpu, sizeof s_gpu);
}
static void restore(const Snapshot *state)
{
    memcpy(&s_vp, &state->vp, sizeof s_vp); memcpy(s_reg, state->reg, sizeof s_reg);
    memcpy(s_reg_dirty, state->dirty, sizeof s_reg_dirty); memcpy(&s_gpu, &state->gpu, sizeof s_gpu);
}
static unsigned cases;
static void compare(uint32_t method, uint32_t count, const uint32_t *source,
    uint32_t load, unsigned pattern, uint32_t subch, int noninc)
{
    restore(&initial); s_vp.const_load = load;
    s_vp.gen = pattern & 1 ? UINT32_MAX : 123;
    s_vp.const_gen = pattern & 2 ? UINT32_MAX : 0;
    Snapshot before; capture(&before);
    bulk = 0;
    uint32_t result = nv2a_pb_exec_run(subch, method, noninc, source, count);
    capture(&expected); restore(&before); bulk = 1;
    assert(nv2a_pb_exec_run(subch, method, noninc, source, count) == result);
    capture(&actual);
    if (memcmp(&expected, &actual, sizeof actual)) {
        fprintf(stderr, "mismatch method=%x count=%u load=%u pattern=%u\n", method, count, load, pattern); abort();
    }
    cases++;
}
static uint64_t cpu_ns(void)
{
    struct timespec now; assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now));
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}
/* Match the normal cross-translation-unit call from nv2a_pb_scan.c. */
static uint32_t (*volatile packet)(uint32_t, uint32_t, int, const uint32_t *, uint32_t) = nv2a_pb_exec_run;
static void benchmark(void)
{
    uint32_t input[32];
    for (unsigned i = 0; i < 32; i++) input[i] = i * 0x1234567u + 0x3f800000u;
    const unsigned sizes[] = {4, 8, 16, 32};
    const unsigned repeats = 3000000;
    for (unsigned size = 0; size < 4; size++) for (unsigned shifted = 0; shifted < 2; shifted++)
        for (unsigned changed = 0; changed < 2; changed++) {
        unsigned slot = shifted ? 15 : 0;
        if (slot + sizes[size] > 32) continue;
        for (unsigned pair = 0; pair < 3; pair++) for (unsigned order = 0; order < 2; order++) {
            bulk = (int)((pair + order) % 2);
            restore(&initial);
            uint32_t words = sizes[size];
            uint64_t start = cpu_ns();
            unsigned sum = 0;
            for (unsigned i = 0; i < repeats; i++) {
                s_vp.const_load = 96; input[0] += changed;
                sum += packet(0, NV097_SET_TRANSFORM_CONSTANT + slot * 4, 0, input, words);
            }
            uint64_t elapsed = cpu_ns() - start;
            assert(sum == repeats * words && s_reg[NV097_SET_TRANSFORM_CONSTANT / 4 + slot] == input[0]);
            assert(!memcmp((uint8_t *)s_vp.c + 96 * 16 + (slot % 4) * 4, input, words * 4));
            printf("words=%u slot=%u changed=%u pair=%u bulk=%d cpu_ms=%.3f ns_packet=%.3f checksum=%u\n",
                words, slot, changed, pair, bulk, elapsed / 1e6, (double)elapsed / repeats, sum);
        }
    }
}
int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--selector")) {
        assert(production_selector() == atoi(argv[2]));
        unsigned reads = property_reads;
        assert(production_selector() == atoi(argv[2]) && reads == property_reads); return 0;
    }
    memset(&s_vp, 0x5a, sizeof s_vp); memset(&s_gpu, 0xa5, sizeof s_gpu);
    s_gpu.prim = 5; s_gpu.idx_count = 0;
    for (unsigned i = 0; i < sizeof s_reg / sizeof *s_reg; i++) s_reg[i] = i * 2654435761u;
    const uint8_t dirty[] = {0, 1, 0x80, 0xff};
    for (unsigned i = 0; i < sizeof s_reg_dirty; i++) s_reg_dirty[i] = dirty[i % 4];
    capture(&initial);
    if (argc == 2 && !strcmp(argv[1], "--bench")) { benchmark(); return 0; }
    assert(argc == 1);
    const uint32_t loads[] = {0, 1, 2, 7, 96, 182, 183, 184, 185, 186, 187, 188,
        189, 190, 191, 192, 193, 255, 0x80000000u, UINT32_MAX - 8, UINT32_MAX - 7,
        UINT32_MAX - 6, UINT32_MAX - 5, UINT32_MAX - 4, UINT32_MAX - 3, UINT32_MAX - 2, UINT32_MAX - 1, UINT32_MAX};
    for (unsigned offset = 0; offset < 4; offset++) for (uint32_t slot = 0; slot < 32; slot++)
        for (uint32_t count = 1; count <= 32 - slot; count++) {
            uint32_t *allocation = malloc((count + offset) * 4); assert(allocation);
            uint32_t *source = allocation + offset;
            for (unsigned pattern = 0; pattern < 5; pattern++) {
                for (unsigned i = 0; i < count; i++) {
                    uint32_t value = initial.reg[NV097_SET_TRANSFORM_CONSTANT / 4 + slot + i];
                    if (pattern == 1 || (pattern == 2 && !i) || (pattern == 3 && i + 1 == count)) value ^= 0xffffffffu;
                    if (pattern == 4) {
                        const uint32_t bits[] = {0, 0x80000000u, 0x7f800000u, 0xff800000u, 0x7fc00001u, 0x7f800001u, 1, 0xffffffffu};
                        value = bits[i % 8];
                    }
                    source[i] = value;
                }
                for (unsigned load = 0; load < sizeof loads / sizeof *loads; load++)
                    compare(NV097_SET_TRANSFORM_CONSTANT + slot * 4, count, source, loads[load], pattern, 0, 0);
            }
            free(allocation);
        }
    uint32_t data[32] = {0};
    for (uint32_t count = 0; count <= 2047; count++) {
        compare(NV097_SET_TRANSFORM_CONSTANT, count, count && count <= 32 ? data : NULL, 0, 0, 0, 0);
        compare(NV097_SET_TRANSFORM_CONSTANT, count, NULL, 0, 0, 1, 0);
        compare(NV097_SET_TRANSFORM_CONSTANT, count, NULL, 0, 0, 0, 1);
    }
    for (unsigned i = 0; i < 8; i++) {
        const uint32_t methods[] = {0, 0xb7c, 0xc00, 0x1ffc, UINT32_MAX, 0xfffffff0u, 0xfffffb80u, 0xbfc};
        bulk = 1;
        assert(!nv2a_pb_exec_run(0, methods[i], 0, NULL, UINT32_MAX));
    }
    verbose = 1; compare(NV097_SET_TRANSFORM_CONSTANT, 32, NULL, 0, 0, 0, 0);
    verbose = 0;
    compare(NV097_ARRAY_ELEMENT16, 3, data, 0, 0, 0, 1);
    printf("PASS: %u production scalar/bulk constant packet cases, bounds, bit patterns, dirty bytes, counters, wrap, fallback and indices\n", cases);
}
