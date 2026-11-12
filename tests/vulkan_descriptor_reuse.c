#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NFS_LOG_INFO(...) ((void)0)
#define NFS_LOG_FATAL(...) ((void)0)

typedef struct {
    VkDescriptorPool descriptors;
    struct AndroidDescriptorReuse *descriptor_reuse;
    VkFramebuffer *framebuffers;
    uint32_t nframebuffers;
} Frame;
static Frame frames[2], *s_f = &frames[0];
static VkDevice s_dev;
static VkDescriptorSetLayout s_dsl = (VkDescriptorSetLayout)(uintptr_t)1;
static VkDescriptorSetLayout s_pp_dsl = (VkDescriptorSetLayout)(uintptr_t)2;
static uint32_t s_frame;
static int s_android_dynamic_descriptors, s_android_descriptor_reuse = 1;
static unsigned allocations, updates, binds, resets, creates;
static int fail_create, fail_allocate;

typedef struct {
    VkDescriptorSetLayout layout;
    unsigned pool, owner;
    int alive, recorded, updated;
    VkDescriptorBufferInfo buffers[3];
    VkDescriptorImageInfo images[4];
} MockSet;
static MockSet sets[40000];
static struct { unsigned count; int alive; } pools[16];
static VkDescriptorSet last_set;
static VkDescriptorBufferInfo expected_buffers[3];
static VkDescriptorImageInfo expected_images[4];

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice device,
    const VkDescriptorPoolCreateInfo *info, const VkAllocationCallbacks *allocator, VkDescriptorPool *pool)
{
    (void)device; (void)allocator;
    assert(!info->flags && info->maxSets == 8192 && info->poolSizeCount == 2);
    assert(info->pPoolSizes[0].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    if (fail_create) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    assert(creates + 1 < 16);
    *pool = (VkDescriptorPool)(uintptr_t)++creates;
    pools[creates].alive = 1;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice device,
    const VkDescriptorSetAllocateInfo *info, VkDescriptorSet *out)
{
    (void)device;
    assert(info->descriptorSetCount == 1);
    unsigned pool = (unsigned)(uintptr_t)info->descriptorPool;
    assert(pool && pool <= creates && pools[pool].alive);
    if (fail_allocate) return VK_ERROR_OUT_OF_HOST_MEMORY;
    assert(pools[pool].count++ < 8192 && allocations + 1 < 40000);
    *out = (VkDescriptorSet)(uintptr_t)++allocations;
    sets[allocations] = (MockSet){.layout = *info->pSetLayouts, .pool = pool,
        .owner = (unsigned)(s_f - frames), .alive = 1};
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice device,
    VkDescriptorPool pool, VkDescriptorPoolResetFlags flags)
{
    (void)device; assert(!flags);
    unsigned id = (unsigned)(uintptr_t)pool;
    for (unsigned i = 1; i <= allocations; i++) if (sets[i].pool == id) {
        assert(!sets[i].recorded); sets[i].alive = 0;
    }
    pools[id].count = 0; resets++;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice device,
    VkFramebuffer framebuffer, const VkAllocationCallbacks *allocator)
{
    (void)device; (void)framebuffer; (void)allocator; assert(0);
}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice device, uint32_t count,
    const VkWriteDescriptorSet *writes, uint32_t copies, const VkCopyDescriptorSet *copy)
{
    (void)device; (void)copy; assert(!copies && (count == 7 || count == 1));
    MockSet *set = &sets[(uintptr_t)writes[0].dstSet];
    assert(set->alive && !set->recorded && set->owner == (unsigned)(s_f - frames));
    assert(set->layout == (count == 7 ? s_dsl : s_pp_dsl));
    for (unsigned i = 0; i < count; i++) {
        const VkWriteDescriptorSet *w = &writes[i];
        assert(w->dstSet == writes[0].dstSet && w->dstBinding == i && w->descriptorCount == 1);
        if (count == 7 && (i < 2 || i == 6)) {
            assert(w->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            set->buffers[i == 6 ? 2 : i] = *w->pBufferInfo;
        } else {
            assert(w->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            set->images[count == 7 ? i - 2 : 0] = *w->pImageInfo;
        }
    }
    set->updated = 1; updates++;
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer command,
    VkPipelineBindPoint point, VkPipelineLayout layout, uint32_t first, uint32_t count,
    const VkDescriptorSet *bound, uint32_t dynamic_count, const uint32_t *offsets)
{
    (void)command; (void)point; (void)layout;
    assert(!first && count == 1 && !dynamic_count && !offsets);
    MockSet *set = &sets[(uintptr_t)*bound];
    assert(set->alive && set->updated && !set->recorded);
    if (set->layout == s_dsl) {
        assert(!memcmp(set->buffers, expected_buffers, sizeof expected_buffers));
        assert(!memcmp(set->images, expected_images, sizeof expected_images));
    } else assert(!memcmp(&set->images[0], &expected_images[0], sizeof expected_images[0]));
    set->recorded = 1; set->updated = 0; last_set = *bound; binds++;
}

#include "nv2a_vk_descriptor_reuse.h"
static void android_descriptor_cache_reset(Frame *frame) { (void)frame; }
#include "descriptor_reset_under_test.h"
#include "descriptor_adapter_under_test.h"

#define PROP_VALUE_MAX 92
static int __system_property_get(const char *name, char *out)
{
    assert(!strcmp(name, "debug.nfsu.descriptor_reuse"));
    const char *property = getenv("REUSE_TEST_PROPERTY");
    return snprintf(out, PROP_VALUE_MAX, "%s", property ? property : "");
}
#include "descriptor_selector_under_test.h"

static void complete_and_begin(unsigned slot)
{
    s_f = &frames[slot];
    for (unsigned i = 1; i <= allocations; i++)
        if (sets[i].owner == slot) sets[i].recorded = 0;
    android_frame_reset(s_f); s_frame++;
}
static void draw(unsigned value, int present, unsigned command_buffer)
{
    VkWriteDescriptorSet writes[7] = {0};
    for (unsigned i = 0; i < 3; i++)
        expected_buffers[i] = (VkDescriptorBufferInfo){(VkBuffer)(uintptr_t)(10 + value % 3),
            (VkDeviceSize)(value + i) * 64, 16 + (value % 3) * 16};
    for (unsigned i = 0; i < 4; i++)
        expected_images[i] = (VkDescriptorImageInfo){(VkSampler)(uintptr_t)(20 + value % 2),
            (VkImageView)(uintptr_t)(30 + value + i), VK_IMAGE_LAYOUT_GENERAL};
    unsigned count = present ? 1 : 7;
    for (unsigned i = 0; i < count; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstBinding = i; writes[i].descriptorCount = 1;
        if (!present && (i < 2 || i == 6)) {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[i].pBufferInfo = &expected_buffers[i == 6 ? 2 : i];
        } else {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &expected_images[present ? 0 : i - 2];
        }
    }
    android_push_descriptors((VkCommandBuffer)(uintptr_t)command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS, VK_NULL_HANDLE, 0, count, writes);
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--selector")) {
        assert(android_descriptor_reuse_requested() == atoi(argv[2])); return 0;
    }
    assert(argc == 1);
    for (unsigned frame = 0; frame < 8; frame++) {
        complete_and_begin(frame % 2);
        unsigned before = allocations;
        for (unsigned draw_id = 0; draw_id < 129; draw_id++)
            draw(frame * 200 + draw_id, draw_id == 64, draw_id < 80 ? 1 : 2);
        if (frame >= 2) assert(allocations == before);
    }
    assert(allocations == 258 && creates == 2 && !resets && updates == binds && binds == 8 * 129);
    assert(s_android_descriptor_reuse_stats.reused == 6 * 129);
    /* A fence-complete frame can update the same handles with new contents. */
    complete_and_begin(0); draw(999, 1, 1);
    VkDescriptorSet present = last_set;
    draw(1000, 0, 1); assert(last_set != present);
    complete_and_begin(0); draw(1001, 0, 1); draw(1002, 1, 1);
    assert(last_set == present);
    /* Preserve all 8192 uses even if layout demand changes between frames.
     * Retained guest/present high-water marks can need a second pool. */
    complete_and_begin(0);
    for (unsigned i = 0; i < 8192; i++) draw(i, 0, i / 1000 + 1);
    VkDescriptorSet set = (VkDescriptorSet)(uintptr_t)999;
    assert(android_descriptor_reuse_get(s_f, s_pp_dsl, 1, &set) == VK_ERROR_OUT_OF_POOL_MEMORY && !set);
    complete_and_begin(0);
    for (unsigned i = 0; i < 8192; i++) draw(i + 9000, 1, 1);
    assert(s_f->descriptor_reuse->pool_count == 2);
    complete_and_begin(0);
    unsigned before = allocations;
    for (unsigned i = 0; i < 8192; i++) draw(i + 20000, i % 2, i / 500 + 1);
    assert(allocations == before && !resets);
    /* Layout mismatches and driver failures never hand out a bad set. */
    complete_and_begin(0);
    assert(android_descriptor_reuse_get(s_f, (VkDescriptorSetLayout)(uintptr_t)123, 0, &set)
        == VK_ERROR_INITIALIZATION_FAILED && !set);
    complete_and_begin(1);
    Frame failed = {0};
    fail_create = 1;
    assert(android_descriptor_reuse_get(&failed, s_dsl, 0, &set) == VK_ERROR_OUT_OF_DEVICE_MEMORY && !set);
    fail_create = 0; fail_allocate = 1;
    assert(android_descriptor_reuse_get(&failed, s_dsl, 0, &set) == VK_ERROR_OUT_OF_HOST_MEMORY && !set);
    assert(!failed.descriptor_reuse->used && !failed.descriptor_reuse->lists[0].count);
    fail_allocate = 0;
    assert(android_descriptor_reuse_get(&failed, s_dsl, 0, &set) == VK_SUCCESS && set);
    /* Explicit rollback still allocates/updates/binds and resets normally. */
    s_android_descriptor_reuse = 0;
    complete_and_begin(1); before = allocations;
    draw(1, 0, 1); draw(2, 1, 2);
    assert(allocations == before + 2);
    complete_and_begin(1); before = allocations;
    draw(3, 0, 1); assert(allocations == before + 1 && resets == 1);
    complete_and_begin(0); complete_and_begin(1);
    for (unsigned f = 0; f < 3; f++) {
        AndroidDescriptorReuse *reuse = (f == 2 ? &failed : &frames[f])->descriptor_reuse;
        if (reuse) { free(reuse->lists[0].sets); free(reuse->lists[1].sets); free(reuse); }
    }
    puts("Descriptor allocations: fenced frame ownership, unique sets per recording, ordinary writes, layout isolation, high-water growth, capacity, failures and rollback: PASS");
}
