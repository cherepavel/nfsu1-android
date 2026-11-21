#define _POSIX_C_SOURCE 200809L
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fenv.h>
#include <time.h>
#include "../xboxrecomp/src/kernel/nv2a_backend.h"
#define __ANDROID__ 1
#define RING_BYTES (2u << 20)
typedef struct { uint32_t w, h, pw, ph; } VkSurf;
static struct { VkBuffer ring; uint8_t *ring_ptr; VkDeviceSize ring_off; } frames[2], *s_f = frames;
static uint32_t s_ring_gen = 1, s_cb_gen = 1;
static VkDeviceSize s_ubo_align = 256;
static unsigned allocations, flushes;
static int fail_allocation;
#define NFS_LOG_INFO(...) ((void)0)
static VkDeviceSize ring_alloc(VkDeviceSize size, VkDeviceSize alignment, void **map)
{
    allocations++;
    if (fail_allocation) { *map = NULL; return 0; }
    VkDeviceSize at = (s_f->ring_off + alignment - 1) & ~(alignment - 1);
    if (at + size > RING_BYTES) { at = 0; s_ring_gen++; flushes++; }
    assert(at + size <= RING_BYTES); s_f->ring_off = at + size;
    *map = s_f->ring_ptr + at; return at;
}
#include "uniform_types.h"
#include "uniform_colors.h"
#include "uniform_zmax.h"
#define PROP_VALUE_MAX 92
static unsigned property_reads;
static int __system_property_get(const char *name, char *out)
{
    assert(!strcmp(name, "debug.nfsu.uniform_cache")); property_reads++;
    const char *value = getenv("UBO_TEST_PROPERTY");
    return snprintf(out, PROP_VALUE_MAX, "%s", value ? value : "");
}
#define uniform_cache_mode production_selector
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_uniforms.h"
#undef uniform_cache_mode
#include "vulkan_uniform_reference.h"
static uint32_t regs[0x2000 / 4];
static Nv2aRawBatch batch;
static VkSurf surf = {640, 480, 1280, 720};
static float scales[4][2];
static unsigned cases;
static uint8_t expected_vs[sizeof(VsBlock)], expected_fs[sizeof(FsBlock)];
_Static_assert(sizeof(VsBlock) == 112 && sizeof(FsBlock) == 384, "std140 sizes");

static void compare_once(void)
{
    /* Reference bytes are copied before any cache/allocator action can touch
     * their addresses. The caller also tests mutations within one epoch. */
    VkDescriptorBufferInfo old[2], now[2];
    assert(legacy_uniform_upload(&batch, &surf, regs, scales, old));
    memcpy(expected_vs, s_f->ring_ptr + old[0].offset, sizeof expected_vs);
    memcpy(expected_fs, s_f->ring_ptr + old[1].offset, sizeof expected_fs);
    assert(uniform_upload(&batch, &surf, regs, scales, now));
    assert(now[0].range == sizeof(VsBlock) && now[1].range == sizeof(FsBlock));
    assert(now[0].buffer == s_f->ring && now[1].buffer == s_f->ring);
    assert(!memcmp(expected_vs, s_f->ring_ptr + now[0].offset, sizeof expected_vs));
    assert(!memcmp(expected_fs, s_f->ring_ptr + now[1].offset, sizeof expected_fs));
    cases++;
}
static void compare_current(void)
{
    compare_once();
    VkDescriptorBufferInfo now[2]; assert(uniform_upload(&batch, &surf, regs, scales, now));
    unsigned before = allocations;
    VkDescriptorBufferInfo hit[2]; assert(uniform_upload(&batch, &surf, regs, scales, hit));
    assert(!memcmp(hit, now, sizeof hit) && allocations == before);
}
static void warm(void)
{
    s_f->ring_off = 0; s_ring_gen++;
    VkDescriptorBufferInfo output[2];
    assert(uniform_upload(&batch, &surf, regs, scales, output));
}
static void compare(void)
{
    s_f->ring_off = 0; s_ring_gen++; compare_current();
}
static uint32_t random_state = 0x12345678;
static uint32_t next(void)
{
    uint32_t x = random_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return random_state = x;
}
static void bits(float *value, uint32_t word) { memcpy(value, &word, 4); }
static uint64_t cpu_ns(void)
{
    struct timespec time; assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time));
    return (uint64_t)time.tv_sec * 1000000000u + (uint64_t)time.tv_nsec;
}
static int (*volatile upload)(const Nv2aRawBatch *, const VkSurf *, const uint32_t *, const float [4][2], VkDescriptorBufferInfo [2]);
static void benchmark(void)
{
    const unsigned epochs = 256, draws = 2048;
    for (unsigned pattern = 0; pattern < 3; pattern++) for (unsigned pair = 0; pair < 3; pair++)
        for (unsigned order = 0; order < 2; order++) {
            unsigned cached = (pair + order) % 2;
            upload = cached ? uniform_upload : legacy_uniform_upload;
            uint64_t start = cpu_ns(); uint32_t checksum = 0;
            unsigned before = allocations;
            for (unsigned epoch = 0; epoch < epochs; epoch++) {
                s_f->ring_off = 0; s_ring_gen++;
                for (unsigned draw = 0; draw < draws; draw++) {
                    if (pattern == 1 || (pattern == 2 && !(draw % 4))) {
                        regs[0xA60 / 4] = 0xff000000u + epoch * draws + draw;
                        bits(&batch.composite[0], 0x3f800000u + draw);
                    }
                    VkDescriptorBufferInfo output[2];
                    assert(upload(&batch, &surf, regs, scales, output));
                    uint32_t word; memcpy(&word, s_f->ring_ptr + output[1].offset + 64, 4); checksum += word;
                }
            }
            uint64_t elapsed = cpu_ns() - start;
            printf("pattern=%u pair=%u cache=%u ns_draw=%.3f allocations=%u checksum=%u\n",
                pattern, pair, cached, (double)elapsed / (epochs * draws), allocations - before, checksum);
        }
}
int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--selector")) {
        int expected = atoi(argv[2]);
        assert(production_selector() == expected); unsigned reads = property_reads;
        assert(production_selector() == expected && property_reads == reads);
        frames[0].ring = (VkBuffer)(uintptr_t)1;
        frames[0].ring_ptr = malloc(RING_BYTES); assert(frames[0].ring_ptr);
        compare_once(); unsigned before = allocations; compare_once();
        assert(allocations == before + (expected ? 2 : 4));
        assert(s_uniform_stats.vs_hits == (unsigned)expected && s_uniform_stats.fs_hits == (unsigned)expected);
        free(frames[0].ring_ptr); return 0;
    }
    for (unsigned i = 0; i < 2; i++) {
        frames[i].ring = (VkBuffer)(uintptr_t)(i + 1); frames[i].ring_ptr = malloc(RING_BYTES); assert(frames[i].ring_ptr);
    }
    /* Exercise the actual cached selector once, then keep it cached. */
    assert(production_selector() == 1);
    for (unsigned i = 0; i < 4; i++) scales[i][0] = scales[i][1] = 1;
    batch.aa_sx = batch.aa_sy = 1; batch.xform = 2;
    if (argc == 2 && !strcmp(argv[1], "--bench")) { benchmark(); return 0; }
    assert(argc == 1);
    for (unsigned trial = 0; trial < 12000; trial++) {
        for (unsigned i = 0; i < sizeof regs / sizeof *regs; i++) regs[i] = next();
        for (unsigned i = 0; i < 16; i++) bits(&batch.composite[i], next());
        for (unsigned i = 0; i < 4; i++) bits(&batch.vp_offset[i], next());
        bits(&batch.aa_sx, next()); bits(&batch.aa_sy, next()); batch.xform = trial % 4;
        for (unsigned i = 0; i < 4; i++) for (unsigned j = 0; j < 2; j++) bits(&scales[i][j], next());
        surf.w = 1 + next() % 4096; surf.h = 1 + next() % 4096;
        surf.pw = 1 + next() % 8192; surf.ph = 1 + next() % 8192;
        s_ubo_align = 16u << (trial % 6); compare();
    }
    /* Exact cache lifetimes and independent invalidation for every register. */
    VkDescriptorBufferInfo a[2], b[2];
    assert(uniform_upload(&batch, &surf, regs, scales, a));
    unsigned before = allocations; s_cb_gen++;
    assert(uniform_upload(&batch, &surf, regs, scales, b) && allocations == before);
    for (unsigned i = 0; i < sizeof regs / sizeof *regs; i++) {
        warm(); regs[i] ^= 0x01010101u; compare_current(); regs[i] ^= 0x01010101u;
    }
    for (unsigned i = 0; i < 16; i++) { warm(); bits(&batch.composite[i], next()); compare_current(); }
    for (unsigned i = 0; i < 4; i++) { warm(); bits(&batch.vp_offset[i], next()); compare_current(); }
    for (unsigned i = 0; i < 4; i++) for (unsigned j = 0; j < 2; j++) { warm(); bits(&scales[i][j], next()); compare_current(); }
    uint32_t *dimensions[] = {&surf.w, &surf.h, &surf.pw, &surf.ph};
    for (unsigned i = 0; i < 4; i++) { warm(); (*dimensions[i])++; compare_current(); }
    for (unsigned i = 0; i < 4; i++) { warm(); batch.xform = i; compare_current(); }
    const uint32_t edges[] = {0, 0x80000000u, 1, 0x7fc00001u, 0x7f800001u, 0x7f800000u, 0xbf800000u, 0x3f800000u};
    for (unsigned i = 0; i < sizeof edges / sizeof *edges; i++) {
        warm(); bits(&batch.aa_sx, edges[i]); compare_current();
        warm(); bits(&batch.aa_sy, edges[i]); compare_current();
    }
    for (unsigned slot = 0; slot < 2; slot++) {
        s_f = &frames[slot]; s_f->ring_off = 0; /* Same generation, different buffer. */
        before = allocations; assert(uniform_upload(&batch, &surf, regs, scales, a)); assert(allocations == before + 2);
    }
    s_f->ring_off = 0; before = allocations;
    assert(uniform_upload(&batch, &surf, regs, scales, a)); assert(allocations == before + 2);
    s_ring_gen++; before = allocations;
    assert(uniform_upload(&batch, &surf, regs, scales, a)); assert(allocations == before + 2);
    fail_allocation = 1; regs[0xA60 / 4] ^= 1;
    assert(!uniform_upload(&batch, &surf, regs, scales, a)); fail_allocation = 0;
    assert(uniform_upload(&batch, &surf, regs, scales, a));
    for (unsigned i = 0; i < 4; i++) {
        const int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
        warm(); assert(!fesetround(modes[i])); compare_current();
    }
    assert(!fesetround(FE_TONEAREST));
#if defined(__aarch64__)
    /* Native FZ/DN and rounding controls must invalidate a live range too. */
    uint64_t saved_fpcr = uniform_fp_mode();
    const uint32_t fp_controls[] = {1u << 24, 1u << 25, 1u << 22, 1u << 23};
    bits(&batch.aa_sx, 1); bits(&batch.aa_sy, 0x7f800001u);
    for (unsigned i = 0; i < sizeof fp_controls / sizeof *fp_controls; i++) {
        warm(); uint64_t fpcr = saved_fpcr ^ fp_controls[i];
        __asm__ __volatile__("msr fpcr, %0\n\tisb" :: "r"(fpcr) : "memory");
        compare_current();
        __asm__ __volatile__("msr fpcr, %0\n\tisb" :: "r"(saved_fpcr) : "memory");
    }
#endif
    warm();
    uint64_t skipped = s_uniform_stats.vs_skipped + s_uniform_stats.fs_skipped;
    for (unsigned i = 0; i < 40; i++) {
        regs[0xA60 / 4]++; bits(&batch.composite[0], 0x3f800000u + i); compare_once();
    }
    assert(s_uniform_stats.vs_skipped + s_uniform_stats.fs_skipped > skipped);
    for (unsigned i = 0; i < 32; i++) compare_once();
    assert(!s_uniform_vs.skip && !s_uniform_fs.skip);
    compare_current();
    /* A ring epoch change cancels any cooldown immediately. */
    s_uniform_vs.skip = s_uniform_fs.skip = 8;
    compare(); assert(!s_uniform_vs.skip && !s_uniform_fs.skip);
    for (unsigned i = 0; i < 2; i++) free(frames[i].ring_ptr);
    printf("PASS: %u legacy/cached uniform byte comparisons, complete keys, padding, ring/buffer/command lifetimes, rounding and allocation failure\n", cases);
    return 0;
}
