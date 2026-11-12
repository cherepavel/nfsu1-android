/* Real tex_get/free/index/account code with GPU allocation/upload stubs.
 * Snapshot ownership and all hash decisions are production code. */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a_vk_texture_hash.h"
#define __ANDROID__ 1
#include "texture_type.h"
#define NFS_LOG_INFO(...) ((void)0)
#define NFS_LOG_DEBUG(...) ((void)0)
#define VT(...) ((void)0)
#define PROP_VALUE_MAX 92
static uint8_t *guest;
static uint32_t s_frame = 1;
static uint32_t s_hz_texs;
static uint64_t s_hz_tex_bytes;
static int s_trace, s_have_bc, mode, fail_image, fail_snapshot, s_tex_changes = -1;
static unsigned creates, uploads, retired, allocations;
static ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)guest; }
static int __system_property_get(const char *name, char *out)
{
    assert(!strcmp(name, "debug.nfsu.tex_samples"));
    const char *value = getenv("TEXS_TEST_PROPERTY");
    return snprintf(out, PROP_VALUE_MAX, "%s", value ? value : "");
}
static void *snapshot_allocate(size_t size)
{
    assert(size == sizeof(VkTextureHashSnapshot));
    if (fail_snapshot) return NULL;
    allocations++; return malloc(size);
}
#define malloc snapshot_allocate
#define texture_snapshot_enabled production_selector
#include "texture_globals.h"
#undef texture_snapshot_enabled
#undef malloc
static int texture_snapshot_enabled(void) { return mode; }
#include "texture_hash_reference.h"
static uint32_t bytes_hash(uint32_t va, uint32_t bytes) { return legacy_hash(va, bytes); }
#include "texture_bytes.h"
static void garbage_add(VkImage image, VkImageView view, VkDeviceMemory memory)
{ assert(image && view && memory); retired++; }
#include "texture_free.h"
static uint64_t nfsu_perf_begin(void) { return 0; }
enum { NFSU_PERF_TEX_HASH, NFSU_PERF_TEX_IMAGE, NFSU_PERF_TEX_MIPS };
static void tex_perf_end(int metric, uint64_t start) { (void)metric; (void)start; }
static int make_image_ml(uint32_t w, uint32_t h, uint32_t layers, uint32_t levels,
    VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
    VkImage *image, VkImageView *view, VkDeviceMemory *memory)
{
    (void)w; (void)h; (void)layers; (void)levels; (void)format; (void)usage; (void)aspect;
    if (fail_image) return 0;
    *image = (VkImage)(uintptr_t)++creates;
    *view = (VkImageView)(uintptr_t)(creates + 10000);
    *memory = (VkDeviceMemory)(uintptr_t)(creates + 20000); return 1;
}
static void tex_fill(VkTex *texture, uint32_t va, uint32_t layer)
{ assert(texture->image && va); (void)layer; uploads++; }
static void gen_mips(VkImage image, uint32_t w, uint32_t h, uint32_t levels, uint32_t layers)
{ assert(image && w && h && levels && layers); }
#include "texture_get.h"

typedef struct { uint32_t hash, checked; unsigned creates, uploads, retired; } Result;
static Result results[4000];
static unsigned result_count;
static void observe(VkImageView view, uint32_t va, int record)
{
    VkTex *texture = NULL;
    for (unsigned i = 0; i < VK_MAX_TEX; i++) if (s_tex[i].image && s_tex[i].va == va) { texture = &s_tex[i]; break; }
    Result result = {texture ? texture->hash : 0, texture ? texture->checked : 0, creates, uploads, retired};
    assert(!texture || view == texture->view);
    if (record) results[result_count] = result;
    else assert(!memcmp(&results[result_count], &result, sizeof result));
    assert(++result_count < 4000);
}
static void cleanup(void)
{
    for (unsigned i = 0; i < VK_MAX_TEX; i++) if (s_tex[i].image) tex_free(&s_tex[i]);
    assert(!s_texture_snapshot_live && !s_tex_bytes);
    memset(s_tex_head, 0, sizeof s_tex_head); memset(s_tex_next, 0, sizeof s_tex_next);
    memset(&s_texture_snapshot_stats, 0, sizeof s_texture_snapshot_stats);
    creates = uploads = retired = allocations = result_count = 0;
    fail_image = fail_snapshot = 0; s_frame = 1;
}
static void run(int enabled, int record)
{
    cleanup(); mode = enabled;
    for (uint32_t i = 0; i < (4u << 20); i++) guest[i] = (uint8_t)(i * 193u + i / 251);
    uint32_t va = 0x10000;
    VkImageView view;
#define GET(color,w,h,pitch,face,levels) do { view = tex_get(va,color,w,h,pitch,face,levels); observe(view,va,record); } while (0)
    GET(0x12,32,16,0,0,1);
    for (unsigned i = 0; i < 20; i++) { s_frame++; GET(0x12,32,16,0,0,1); }
    if (enabled) assert(s_texture_snapshot_stats.hits == 20 && allocations == 1);
    if (enabled) {
        for (unsigned i = 0; i < VK_MAX_TEX; i++)
            if (s_tex[i].image && s_tex[i].va == va) s_tex[i].snapshot->hash ^= 1;
    }
    s_frame++; GET(0x12,32,16,0,0,1); /* Inconsistent metadata forces recapture. */
    /* Same-frame mutation remains deferred, matching the existing policy. */
    guest[va] ^= 3; GET(0x12,32,16,0,0,1);
    s_frame++; GET(0x12,32,16,0,0,1);
    s_frame++; guest[va + 4] ^= 7; GET(0x12,32,16,0,0,1); /* Unsampled byte. */
    /* Two different sample sets with identical FNV hashes: preserve the old
     * hash-hit decision and refresh the snapshot after its comparison fails. */
    uint32_t a, b; memcpy(&a, guest + va, 4); memcpy(&b, guest + va + 8, 4);
    uint32_t seed = 2166136261u ^ 2048u;
    uint32_t old = ((seed ^ a) * 16777619u) ^ b;
    a ^= 0x12345678u; b = ((seed ^ a) * 16777619u) ^ old;
    unsigned before = uploads;
    memcpy(guest + va, &a, 4); memcpy(guest + va + 8, &b, 4);
    s_frame++; GET(0x12,32,16,0,0,1); assert(uploads == before);
    s_frame++; GET(0x12,32,16,0,0,1);
    s_frame++; GET(0x12,32,16,128,0,2);
    guest[va] ^= 0xa1; s_frame++; GET(0x12,32,16,128,0,2);
    s_palette_va = 0x20000; va = 0x30000;
    GET(0x0b,32,32,0,0,1);
    s_frame++; GET(0x0b,32,32,0,0,1);
    guest[s_palette_va + 19] ^= 1;
    s_frame++; GET(0x0b,32,32,0,0,1);
    memcpy(guest + s_palette_va + 4096, guest + s_palette_va, 1024); s_palette_va += 4096;
    s_frame++; GET(0x0b,32,32,0,0,1);
    va = 0x50000; GET(0x0c,32,32,0,512,2);
    s_frame++; GET(0x0c,32,32,0,512,2);
    const unsigned widths[] = {255, 256, 1024, 1025};
    for (unsigned i = 0; i < 4; i++) {
        va = 0x90000 + i * 0x4000; unsigned before_allocations = allocations;
        GET(0x12,widths[i],1,0,0,1);
        if (enabled) assert(allocations == before_allocations + (i == 1 || i == 2));
    }
    va = 0x80000; unsigned allocated = allocations;
    GET(0x12,64,64,0,0,1); s_frame++; GET(0x12,64,64,0,0,1);
    assert(allocations == allocated);
    fail_image = 1; va = 0x60000; GET(0x12,16,16,0,0,1);
    fail_image = 0; GET(0x12,16,16,0,0,1);
    fail_snapshot = 1; va = 0x70000; GET(0x12,16,16,0,0,1);
    s_frame++; GET(0x12,16,16,0,0,1);
    fail_snapshot = 0; s_frame++; GET(0x12,16,16,0,0,1);
    s_frame++; GET(0x12,16,16,0,0,1);
    /* Fill and overrun the cache; tex_free must release each snapshot. */
    for (unsigned i = 0; i < VK_MAX_TEX + 16; i++) {
        va = 0x100000 + i * 1024; s_frame++; GET(0x12,16,16,0,0,1);
    }
    assert(s_texture_snapshot_live <= VK_MAX_TEX);
#undef GET
}
int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--selector")) {
        assert(production_selector() == atoi(argv[2]));
        assert(production_selector() == atoi(argv[2])); return 0;
    }
    assert(argc == 1); guest = malloc(4u << 20); assert(guest);
    run(0, 1); unsigned count = result_count;
    run(1, 0); assert(result_count == count);
    cleanup(); free(guest);
    puts("PASS: production texture cache scalar/snapshot parity, frame policy, palette/cube/format changes, FNV collision fallback, OOM and eviction ownership");
}
