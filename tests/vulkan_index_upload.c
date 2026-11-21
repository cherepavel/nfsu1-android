#define _POSIX_C_SOURCE 200809L
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../xboxrecomp/src/kernel/nv2a_raw_indices.h"

#define RING_BYTES 16384
static struct {
    VkBuffer ring;
    uint8_t bytes[RING_BYTES + 16];
    VkDeviceSize ring_off;
} frame, *s_f = &frame;
static VkCommandBuffer s_cb;
static VkDeviceSize bound_offset;
static VkIndexType bound_type;
static unsigned allocations, bindings, flushes;
static int fail_allocation;
static uint32_t s_ring_gen, s_cb_gen, s_st_calls, s_st_inst, fp_mode, s_frame;
static const VkDeviceSize s_ubo_align = 4;
static int s_in_render, s_native_ui_phase;
typedef struct { uint32_t va, gen, screen_frame; int screen_seen; } VkSurf;
static VkSurf s_surf[2], *s_rt = s_surf;
static struct { int active; } s_native_ui[2];
static struct { uint64_t draws, appended, indices; } s_scene_instances[3];
static unsigned issued_indices, issued_instances;
static uint8_t register_dirty[0x2000 / 64];
#define VK_CONST_SIZE (192 * 16)

uint8_t *nv2a_pb_reg_dirty(void) { return register_dirty; }
static uint32_t uniform_fp_mode(void) { return fp_mode; }
static VkSurf *surf_find(uint32_t va)
{
    for (unsigned i = 0; i < 2; i++)
        if (s_surf[i].va == va) return &s_surf[i];
    return NULL;
}
static void gpu_count_draw(uint32_t indices, uint32_t instances, int eligible, int selected)
{
    assert(indices && instances && !eligible && !selected);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer command,
    uint32_t indices, uint32_t instances, uint32_t first_index, int32_t vertex_offset,
    uint32_t first_instance)
{
    assert(command == s_cb && !first_index && !vertex_offset && !first_instance);
    issued_indices = indices;
    issued_instances = instances;
}

static VkDeviceSize ring_alloc(VkDeviceSize bytes, VkDeviceSize alignment, void **map)
{
    assert(alignment == 4);
    allocations++;
    if (fail_allocation || bytes > RING_BYTES) { *map = NULL; return 0; }
    VkDeviceSize at = (s_f->ring_off + alignment - 1) & ~(alignment - 1);
    if (at + bytes > RING_BYTES) { at = 0; flushes++; }
    s_f->ring_off = at + bytes;
    memset(s_f->bytes + at, 0xA5, (size_t)bytes + 16);
    *map = s_f->bytes + at;
    return at;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer command,
    VkBuffer buffer, VkDeviceSize offset, VkIndexType type)
{
    assert(command == s_cb && buffer == s_f->ring && !(offset & 3));
    assert(type == VK_INDEX_TYPE_UINT16 || type == VK_INDEX_TYPE_UINT32);
    bindings++;
    bound_offset = offset;
    bound_type = type;
}

#include "nv2a_vk_indices.h"
#include "legacy_indices_under_test.h"
#include "index_instancing_under_test.h"

#define PROP_VALUE_MAX 92
#define NFS_LOG_INFO(...) ((void)0)
static int __system_property_get(const char *name, char *value)
{
    const char *property;
    if (!strcmp(name, "debug.nfsu.index_fuse")) property = getenv("INDEX_TEST_FUSE");
    else { assert(!strcmp(name, "debug.nfsu.index_stats")); property = getenv("INDEX_TEST_STATS"); }
    return snprintf(value, PROP_VALUE_MAX, "%s", property ? property : "");
}
#include "index_selector_under_test.h"

static uint32_t source[2052], scratch[2052], saved_source[2052];
static uint32_t expected[4096], actual[4104];
static unsigned cases;
static uint32_t random_state = 1234567;
static uint32_t random_word(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

static void test_register_comparison(void)
{
    uint32_t regs[0x2000 / 4] = {0};
    for (uint32_t method = 0; method < 0x2000; method += 4) {
        uint32_t word = method / 4;
        uint32_t block = method / 64;
        int upload = method >= 0xB00 && method < 0xC00;
        int ignored = upload || method == 0x0100 || method == 0x17FC
            || (method >= 0x1800 && method < 0x1820)
            || method == 0x1D6C || method == 0x1D70
            || method == 0x1E9C || method == 0x1EA4;
        regs[word] = random_word() | 1u;
        register_dirty[block] = 0x80;
        assert(regs_changed(regs) == !ignored);
        assert(!register_dirty[block]);
        assert(s_regs_seen[word] == (upload ? 0 : regs[word]));
        register_dirty[block] = 1;
        assert(!regs_changed(regs));
    }

    regs[0] ^= 1;
    regs[0x1FFCu / 4] ^= 1;
    regs[0x1804 / 4] ^= 1;
    memset(register_dirty, 1, sizeof register_dirty);
    assert(regs_changed(regs));
    assert(!regs_changed(regs));
    for (unsigned i = 0; i < sizeof register_dirty; i++)
        assert(!register_dirty[i]);
    assert(s_regs_seen[0] == regs[0]);
    assert(s_regs_seen[0x1FFCu / 4] == regs[0x1FFCu / 4]);
    assert(s_regs_seen[0x1804 / 4] == regs[0x1804 / 4]);
}

static void compare(uint32_t prim, uint32_t count, uint32_t vertices, uint32_t base)
{
    Nv2aRawBatch original = {0}, direct = {0}, legacy = {0};
    original.prim = prim; original.vertex_count = vertices;
    for (unsigned i = 0; i < count; i++) source[1 + i] = base + random_word();
    memcpy(saved_source, source, sizeof source);
    direct = legacy = original;
    memset(scratch, 0xCC, sizeof scratch);
    nv2a_raw_indices_bind(&direct, 1, source + 1, NULL, count, base);
    assert(direct.indices == source + 1 && direct.index_base == base && direct.index_count == count);
    for (unsigned i = 0; i < sizeof scratch / sizeof scratch[0]; i++) assert(scratch[i] == 0xCCCCCCCC);
    nv2a_raw_indices_bind(&legacy, 0, source + 1, scratch + 1, count, base);
    assert(legacy.indices == scratch + 1 && !legacy.index_base && legacy.index_count == count);
    assert(scratch[0] == 0xCCCCCCCC && scratch[count + 1] == 0xCCCCCCCC);

    VkIndexUpload plan;
    const uint32_t *indices;
    uint32_t n, cls;
    VkPrimitiveTopology topology;
    int old_ok = prim_indices(&legacy, &indices, &n, &topology, &cls);
    int new_ok = index_upload_plan(&direct, &plan);
    if (!old_ok || !n) {
        assert(!new_ok || !plan.count);
        unsigned before = allocations;
        assert(!upload_fused_indices(&direct, &plan) && allocations == before);
        cases++;
        return;
    }
    assert(new_ok && plan.count == n && plan.topology == topology && plan.primitive_class == cls);
    assert(plan.type == (vertices <= 65536 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32));
    unsigned element_bytes = vertices <= 65536 ? 2 : 4;
    assert(plan.bytes == (VkDeviceSize)n * element_bytes);
    memset(expected, 0xA5, sizeof expected);
    if (element_bytes == 2) {
        uint16_t *dst = (uint16_t *)expected;
        for (uint32_t i = 0; i < n; i++) dst[i] = (uint16_t)indices[i];
    } else memcpy(expected, indices, (size_t)n * 4);
    memset(actual, 0xA5, sizeof actual);
    /* Destination starts four bytes in, keeping prefix and suffix canaries. */
    index_upload_fill(&direct, &plan, actual + 1);
    assert(actual[0] == 0xA5A5A5A5);
    assert(!memcmp(actual + 1, expected, (size_t)plan.bytes + 16));
    assert(upload_fused_indices(&direct, &plan));
    assert(bound_type == plan.type);
    assert(!memcmp(s_f->bytes + bound_offset, expected, (size_t)plan.bytes + 16));
    assert(!memcmp(saved_source, source, sizeof source));
    cases++;
}

static volatile uint64_t benchmark_checksum;
static __attribute__((noinline)) void benchmark_legacy(const Nv2aRawBatch *b)
{
    Nv2aRawBatch legacy = *b;
    nv2a_raw_indices_bind(&legacy, 0, b->indices, scratch, b->index_count, b->index_base);
    const uint32_t *indices;
    uint32_t n, cls;
    VkPrimitiveTopology topology;
    assert(prim_indices(&legacy, &indices, &n, &topology, &cls));
    uint16_t *dst = (uint16_t *)actual;
    for (uint32_t i = 0; i < n; i++) dst[i] = (uint16_t)indices[i];
}

static __attribute__((noinline)) void benchmark_fused(const Nv2aRawBatch *b)
{
    VkIndexUpload plan;
    assert(index_upload_plan(b, &plan));
    index_upload_fill(b, &plan, actual);
}

static double benchmark(void (*run)(const Nv2aRawBatch *), Nv2aRawBatch *b)
{
    struct timespec start, end;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
    for (unsigned i = 0; i < 100000; i++) {
        source[0] = b->index_base + (i & 65535);
        run(b);
        benchmark_checksum += ((uint16_t *)actual)[i % b->index_count];
    }
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1e6;
}

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "--selector")) {
        assert(index_fuse_enabled() == atoi(argv[2]));
        assert(index_stats_enabled() == atoi(argv[3]));
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--benchmark")) {
        Nv2aRawBatch b = {0};
        b.indices = source; b.index_count = 2048; b.index_base = 70000; b.vertex_count = 65536;
        for (unsigned i = 0; i < b.index_count; i++) source[i] = b.index_base + i;
        for (unsigned prim = 5; prim <= 8; prim += 3) {
            b.prim = prim;
            for (unsigned pair = 0; pair < 3; pair++) {
                benchmark_checksum = 0;
                double old_ms = benchmark(benchmark_legacy, &b);
                uint64_t old_sum = benchmark_checksum;
                benchmark_checksum = 0;
                double new_ms = benchmark(benchmark_fused, &b);
                assert(old_sum == benchmark_checksum);
                printf("primitive=%u pair=%u legacy_cpu_ms=%.3f fused_cpu_ms=%.3f checksum=%llu\n",
                    prim, pair + 1, old_ms, new_ms, (unsigned long long)old_sum);
            }
        }
        free(s_prim_idx);
        return 0;
    }
    assert(argc == 1);
    test_register_comparison();
    assert(!nv2a_raw_indices_direct(0, 0));
    assert(!nv2a_raw_indices_direct(NV2A_BACKEND_RAW_DIRECT, 0));
    assert(nv2a_raw_indices_direct(NV2A_BACKEND_INDEX_BASE, 0));
    assert(!nv2a_raw_indices_direct(NV2A_BACKEND_INDEX_BASE, 1));
    assert(nv2a_raw_indices_direct(NV2A_BACKEND_INDEX_BASE | NV2A_BACKEND_RAW_DIRECT, 0));
    const uint32_t bases[] = {0, 1, 65535, 65536, 0x00FFFF00, 0x80000000, UINT32_MAX - 7};
    const uint32_t widths[] = {0, 1, 65535, 65536, 65537, 1048576};
    for (unsigned p = 0; p <= 11; p++)
        for (unsigned n = 0; n <= 69; n++)
            for (unsigned b = 0; b < sizeof bases / sizeof bases[0]; b++)
                for (unsigned v = 0; v < sizeof widths / sizeof widths[0]; v++)
                    compare(p, n, widths[v], bases[b]);
    for (unsigned i = 0; i < 10000; i++) {
        uint32_t prim = 1 + random_word() % 10;
        uint32_t count = random_word() % 2049;
        uint32_t width = widths[random_word() % 6];
        compare(prim, count, width, random_word());
    }

    /* Explicit full-width values and exact 65536-vertex boundary. */
    source[0] = 70000; source[1] = 70000 + 65535; source[2] = 70000 + 65536;
    Nv2aRawBatch b = {0};
    b.prim = 5; b.indices = source; b.index_count = 3; b.index_base = 70000; b.vertex_count = 65536;
    VkIndexUpload plan;
    assert(index_upload_plan(&b, &plan) && plan.type == VK_INDEX_TYPE_UINT16);
    index_upload_fill(&b, &plan, actual);
    assert(((uint16_t *)actual)[0] == 0 && ((uint16_t *)actual)[1] == 65535 && ((uint16_t *)actual)[2] == 0);
    b.vertex_count++;
    assert(index_upload_plan(&b, &plan) && plan.type == VK_INDEX_TYPE_UINT32);
    index_upload_fill(&b, &plan, actual);
    assert(actual[0] == 0 && actual[1] == 65535 && actual[2] == 65536);
    /* Allocation failure binds nothing and never dereferences source. */
    b.indices = NULL; fail_allocation = 1;
    unsigned before = bindings;
    assert(!upload_fused_indices(&b, &plan) && bindings == before);
    fail_allocation = 0;
    b.prim = 3; b.index_count = UINT32_MAX;
    assert(!index_upload_plan(&b, &plan));
    b.prim = 8;
    assert(!index_upload_plan(&b, &plan));
    b.index_count = 0;
    assert(index_upload_plan(&b, &plan) && !plan.count);
    assert(!upload_fused_indices(&b, &plan));
    index_upload_fill(&b, &plan, NULL);
    assert(flushes > 0);

    /* Matching original indices with different bases must not instance. */
    memset(&s_pend, 0, sizeof s_pend);
    memset(&b, 0, sizeof b);
    b.xform = 2; b.prim = 5; b.indices = source; b.index_count = 3;
    s_pend.prim = 5; s_pend.index_count = 3; s_pend.idx = source;
    assert(pend_matches(&b));
    b.index_base = 1;
    assert(!pend_matches(&b));
    s_pend.index_base = 1;
    assert(pend_matches(&b));

    /* Reflection instancing keeps the held draw's state and order while
     * taking a separate vertex-program constant block for each instance. */
    memset(&s_pend, 0, sizeof s_pend);
    float first[192][4] = {{1.0f}}, second[192][4] = {{2.0f}};
    void *map;
    frame.ring_off = 0;
    VkDeviceSize constants = ring_alloc(VK_CONST_SIZE, s_ubo_align, &map);
    memcpy(map, first, sizeof first);
    s_surf[0].va = b.color_va = 0x12340000;
    s_in_render = 1;
    pend_hold(&b, 3, constants, 2, 0, 0, 1);
    b.vp_consts = (const float (*)[4])second;
    assert(!pend_append(&b, 1, 1) && !pend_append(&b, 0, 0) && !pend_append(&b, 0, 2));
    s_cb_gen++; assert(!pend_append(&b, 0, 1)); s_cb_gen--;
    s_ring_gen++; assert(!pend_append(&b, 0, 1)); s_ring_gen--;
    s_in_render = 0; assert(!pend_append(&b, 0, 1)); s_in_render = 1;
    b.vp_offset[0] = 1.0f; assert(!pend_append(&b, 0, 1)); b.vp_offset[0] = 0.0f;
    fp_mode++; assert(!pend_append(&b, 0, 1)); fp_mode--;
    b.tex_va[0]++; assert(!pend_append(&b, 0, 1)); b.tex_va[0]--;
    b.vtx_epoch++; assert(!pend_append(&b, 0, 1)); b.vtx_epoch--;
    assert(!issued_instances && frame.ring_off == VK_CONST_SIZE);
    assert(pend_append(&b, 0, 1) && !pend_append(&b, 0, 1));
    assert(!issued_instances && frame.ring_off == 2 * VK_CONST_SIZE);
    assert(!memcmp(frame.bytes, first, sizeof first));
    assert(!memcmp(frame.bytes + VK_CONST_SIZE, second, sizeof second));
    pend_flush();
    assert(issued_indices == 3 && issued_instances == 2 && s_st_calls == 1);
    assert(!s_pend.on && s_surf[0].gen == 1 && s_scene_instances[1].appended == 1);
    free(s_pend.idx);

    /* Main batching uses both recent screen buffers and excludes UI and
     * offscreen draws, including when a color buffer is reused later. */
    uint32_t regs[0x2000 / 4] = {0};
    b.regs = regs;
    b.zeta_va = 0x20000;
    regs[0x30C / 4] = 1;
    s_frame = 100;
    assert(!main_instance_surface(&b));
    s_surf[0].screen_seen = 1;
    s_surf[0].screen_frame = s_frame - 1;
    assert(main_instance_surface(&b));
    s_surf[1] = (VkSurf){ .va = 0x34560000, .screen_frame = s_frame - 2, .screen_seen = 1 };
    b.color_va = s_surf[1].va;
    assert(main_instance_surface(&b));
    s_native_ui[1].active = 1; assert(!main_instance_surface(&b)); s_native_ui[1].active = 0;
    s_native_ui_phase = 1; assert(!main_instance_surface(&b)); s_native_ui_phase = 0;
    regs[0x30C / 4] = 0; assert(!main_instance_surface(&b)); regs[0x30C / 4] = 1;
    s_frame += 3; assert(!main_instance_surface(&b));
    free(s_prim_idx);
    printf("Fused index upload: %u differential cases, bridge/hooks, topology, widths, wrapping, ring failure and instancing: PASS\n", cases);
}
