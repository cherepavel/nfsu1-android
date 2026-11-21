/* Includes the actual upload_vertices, format conversion and range packing
 * extracted by vulkan_vertex_cache.py. The recorder compares vertex fetches,
 * not raw offsets: cached and baseline allocations intentionally differ. */
#define __ANDROID__ 1
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xboxrecomp/src/kernel/nv2a_backend.h"

#define RING_BYTES 65536
static struct {
    VkBuffer ring;
    uint8_t ring_ptr[RING_BYTES];
    VkDeviceSize ring_off;
} frame, *s_f = &frame;
static uint32_t s_ring_gen, s_cb_gen;
static VkCommandBuffer s_cb;
static VkPhysicalDevice s_pd;
static uint32_t android_vertex_strides[NV2A_RAW_ATTRS];
static VkFormat android_vertex_formats[NV2A_RAW_ATTRS];
static VkDeviceSize driver_offsets[NV2A_RAW_ATTRS];
static unsigned mode, allocations, bindings, flushes;
static int s_trace;
#define NFS_LOG_DEBUG(...) ((void)0)

static int vertex_cache_mode(void) { return (int)mode; }

static void new_command_buffer(void)
{
    s_cb = (VkCommandBuffer)(uintptr_t)++s_cb_gen;
    memset(driver_offsets, 0xFF, sizeof driver_offsets);
}

static VkDeviceSize ring_alloc(VkDeviceSize bytes, VkDeviceSize alignment, void **map)
{
    allocations++;
    VkDeviceSize at = (s_f->ring_off + alignment - 1) & ~(alignment - 1);
    if (at + bytes > RING_BYTES) {
        flushes++;
        s_ring_gen++;
        new_command_buffer();
        memset(s_f->ring_ptr, 0xCC, sizeof s_f->ring_ptr);
        at = 0;
        if (bytes > RING_BYTES) { *map = NULL; return 0; }
    }
    s_f->ring_off = at + bytes;
    *map = s_f->ring_ptr + at;
    return at;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice device,
    VkFormat format, VkFormatProperties *properties)
{
    (void)device;
    memset(properties, 0, sizeof *properties);
    /* Exercise the backend conversion even for a nominally direct attribute. */
    if (format != VK_FORMAT_R16G16B16_SSCALED)
        properties->bufferFeatures = VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer cb,
    uint32_t first, uint32_t count, const VkBuffer *buffers, const VkDeviceSize *offsets)
{
    assert(cb == s_cb && first == 0 && count == NV2A_RAW_ATTRS);
    for (unsigned a = 0; a < count; a++) {
        assert(buffers[a] == s_f->ring && offsets[a] < s_f->ring_off);
        driver_offsets[a] = offsets[a];
    }
    bindings++;
}

#include "vertex_upload_under_test.h"

#define PROP_VALUE_MAX 92
#define NFS_LOG_INFO(...) ((void)0)
static int __system_property_get(const char *name, char *value)
{
    assert(!strcmp(name, "debug.nfsu.vertex_cache"));
    const char *property = getenv("VKVTX_TEST_PROPERTY");
    return snprintf(value, PROP_VALUE_MAX, "%s", property ? property : "");
}
#define vertex_cache_mode vertex_cache_startup_mode
#include "vertex_selector_under_test.h"
#undef vertex_cache_mode

static uint8_t vertices[8192], other_vertices[8192];
static float converted[256 * NV2A_RAW_ATTRS * 4];
static uint8_t baseline[NV2A_RAW_ATTRS][256][16];
static uint32_t baseline_strides[NV2A_RAW_ATTRS];
static VkFormat baseline_formats[NV2A_RAW_ATTRS];

static unsigned format_bytes(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
    case VK_FORMAT_R32G32B32_SFLOAT: return 12;
    case VK_FORMAT_R32G32_SFLOAT: return 8;
    case VK_FORMAT_B8G8R8A8_UNORM: return 4;
    default: assert(0); return 0;
    }
}

static void fetch(const Nv2aRawBatch *b, uint8_t out[NV2A_RAW_ATTRS][256][16])
{
    memset(out, 0, sizeof baseline);
    for (unsigned a = 0; a < NV2A_RAW_ATTRS; a++) {
        unsigned size = format_bytes(android_vertex_formats[a]);
        for (unsigned v = 0; v < b->vertex_count; v++) {
            VkDeviceSize offset = driver_offsets[a] + (VkDeviceSize)v * android_vertex_strides[a];
            assert(offset + size <= s_f->ring_off);
            memcpy(out[a][v], s_f->ring_ptr + offset, size);
        }
    }
}

static Nv2aRawBatch batch(void)
{
    Nv2aRawBatch b = {0};
    b.vertex_count = 128;
    b.attr_present = b.attr_direct = (1u << 0) | (1u << 3) | (1u << 9);
    b.direct[0].ptr = vertices + 1; /* unaligned and interleaved spans */
    b.direct[0].type = 2; b.direct[0].size = 3; b.direct[0].stride = 24;
    b.direct[3].ptr = vertices + 13;
    b.direct[3].type = 0; b.direct[3].size = 4; b.direct[3].stride = 24;
    b.direct[9].ptr = other_vertices + 3;
    b.direct[9].type = 2; b.direct[9].size = 2; b.direct[9].stride = 8;
    b.attr_const_gen = 1;
    b.attrs = converted;
    for (unsigned a = 0; a < NV2A_RAW_ATTRS; a++)
        for (unsigned c = 0; c < 4; c++) b.attr_const[a][c] = (float)(10 * a + c);
    return b;
}

static void reset(unsigned selected)
{
    memset(&s_vertex_cache, 0, sizeof s_vertex_cache);
    memset(&s_vertex_stats, 0, sizeof s_vertex_stats);
    memset(&frame, 0, sizeof frame);
    frame.ring = (VkBuffer)(uintptr_t)1;
    s_ring_gen++;
    mode = selected;
    allocations = bindings = flushes = 0;
    new_command_buffer();
}

static void reference(const Nv2aRawBatch *b)
{
    reset(0);
    assert(upload_vertices(b));
    fetch(b, baseline);
    memcpy(baseline_strides, android_vertex_strides, sizeof baseline_strides);
    memcpy(baseline_formats, android_vertex_formats, sizeof baseline_formats);
}

static void check(const Nv2aRawBatch *b)
{
    static uint8_t actual[NV2A_RAW_ATTRS][256][16];
    assert(upload_vertices(b));
    fetch(b, actual);
    assert(!memcmp(baseline, actual, sizeof baseline));
    assert(!memcmp(baseline_strides, android_vertex_strides, sizeof baseline_strides));
    assert(!memcmp(baseline_formats, android_vertex_formats, sizeof baseline_formats));
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--selector")) {
        assert(vertex_cache_startup_mode() == atoi(argv[2]));
        return 0;
    }
    assert(argc == 1);
    for (unsigned i = 0; i < sizeof vertices; i++) {
        vertices[i] = (uint8_t)(i * 17u + 3);
        other_vertices[i] = (uint8_t)(i * 13u + 7);
    }
    Nv2aRawBatch b = batch();
    reference(&b);
    reset(1);
    check(&b);
    VkDeviceSize first_end = s_f->ring_off;
    assert(allocations == 1 && bindings == 1);
    /* Texture/shader/index/target changes do not alter vertex input bytes. */
    b.tex_va[0]++; b.vp_const_gen++; b.index_count++; b.color_va++;
    check(&b);
    assert(allocations == 1 && bindings == 1 && s_f->ring_off == first_end);
    assert(s_vertex_stats.hits == 1 && s_vertex_stats.skipped_binds == 1);
    assert(s_vertex_stats.reused_bytes == s_vertex_stats.copied_bytes);

    new_command_buffer(); /* early submit retains ring bytes, loses bindings */
    memset(android_vertex_strides, 0xAA, sizeof android_vertex_strides);
    memset(android_vertex_formats, 0xAA, sizeof android_vertex_formats);
    check(&b);
    assert(allocations == 1 && bindings == 2);
    /* An unrelated/converted upload replaces bindings; old cache still works. */
    Nv2aRawBatch converted_batch = b;
    converted_batch.attr_direct = 0;
    assert(upload_vertices(&converted_batch));
    unsigned before = allocations;
    check(&b);
    assert(allocations == before && bindings == 4);

    /* Changed bytes after a semaphore/trap/inline epoch must be copied. */
    b.vtx_epoch++;
    vertices[100] ^= 0x80;
    assert(upload_vertices(&b));
    assert(allocations == before + 1);
    assert(s_f->ring_ptr[driver_offsets[0] + 99] == vertices[100]);

    reference(&b);
    reset(1);
    check(&b);
    s_ring_gen++; s_f->ring_off = 0; new_command_buffer();
    memset(s_f->ring_ptr, 0xDD, sizeof s_f->ring_ptr);
    check(&b);
    assert(allocations == 2); /* new frame, including same recycled ring */
    s_f->ring = (VkBuffer)(uintptr_t)2;
    check(&b);
    assert(allocations == 3); /* ring identity is also checked */

    /* Each key component independently prevents a hit. */
    for (unsigned change = 0; change < 7; change++) {
        Nv2aRawBatch changed = b;
        if (change == 0) changed.vertex_count--;
        if (change == 1) changed.direct[0].ptr++;
        if (change == 2) changed.direct[0].stride++;
        if (change == 3) changed.direct[0].size = 4;
        if (change == 4) { changed.attr_const_gen++; changed.attr_const[5][0] += 10.0f; }
        if (change == 5) changed.attr_present = changed.attr_direct &= ~(1u << 9);
        if (change == 6) changed.direct[3].type = 2;
        reference(&changed);
        reset(1);
        assert(upload_vertices(&b));
        before = allocations;
        check(&changed);
        assert(allocations == before + 1);
        check(&changed);
        assert(allocations == before + 1);
    }

    /* Small uploads, constant-only and unsupported/converted formats bypass. */
    for (unsigned variant = 0; variant < 5; variant++) {
        Nv2aRawBatch bypass = b;
        if (variant == 0) bypass.vertex_count = 2;
        if (variant == 1) bypass.attr_direct = 0;
        if (variant == 2) { bypass.direct[0].type = 5; bypass.direct[0].size = 3; }
        if (variant == 3) bypass.attr_present = bypass.attr_direct = 0;
        if (variant == 4) bypass.attr_const_gen = 0;
        reference(&bypass);
        reset(1);
        check(&bypass); check(&bypass);
        assert(allocations == 2 && !s_vertex_stats.eligible && !s_vertex_stats.hits);
    }
    b = batch();
    reference(&b);
    reset(2); /* measure mode retains original allocation and bind counts */
    for (unsigned i = 0; i < 4; i++) check(&b);
    assert(allocations == 4 && bindings == 4 && s_vertex_stats.hits == 3);
    assert(!s_vertex_stats.reused_bytes && !s_vertex_stats.skipped_binds);
    assert(s_vertex_stats.reusable_bytes * 4 == s_vertex_stats.copied_bytes * 3);

    reset(1);
    check(&b);
    /* Ring flush during a miss must not leave an old upload reusable. */
    s_f->ring_off = RING_BYTES - 16;
    Nv2aRawBatch different = b;
    different.direct[0].ptr++;
    assert(upload_vertices(&different) && flushes == 1);
    check(&b);
    assert(allocations == 3);

    Nv2aRawBatch failed = b;
    failed.attr_direct = 0;
    failed.attr_present = UINT16_MAX;
    failed.vertex_count = 256; /* converted payload exceeds the test ring */
    assert(!upload_vertices(&failed));
    before = allocations;
    check(&b);
    assert(allocations == before + 1); /* failure's ring reset invalidates old entries */
    failed.vertex_count = 0;
    before = allocations;
    assert(!upload_vertices(&failed) && allocations == before);

    /* Mixed meshes and cache collisions: fetches must match source bytes. */
    reset(1);
    for (unsigned i = 0; i < 10000; i++) {
        Nv2aRawBatch mesh = b;
        mesh.direct[0].ptr += i % 113;
        if (!(i % 37)) mesh.vtx_epoch = ++b.vtx_epoch;
        else mesh.vtx_epoch = b.vtx_epoch;
        assert(upload_vertices(&mesh));
        for (unsigned v = 0; v < mesh.vertex_count; v++)
            assert(!memcmp(s_f->ring_ptr + driver_offsets[0] + v * 24,
                mesh.direct[0].ptr + v * 24, 12));
    }
    puts("Vertex upload cache: fetch equivalence, conversion bypass, epochs, rings, CBs, collisions and measurement mode: PASS");
}
