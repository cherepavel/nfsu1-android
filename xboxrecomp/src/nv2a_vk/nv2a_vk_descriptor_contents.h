/* Consecutive ordinary-descriptor requests in one command-buffer recording.
 * The last set remains immutable and bound: a hit issues no allocation,
 * update or bind. No resource reference survives a frame/pool reset. */
#ifndef NV2A_VK_DESCRIPTOR_CONTENTS_H
#define NV2A_VK_DESCRIPTOR_CONTENTS_H
#include <stdint.h>
#include <string.h>

typedef struct {
    /* Three UBO triples and four sampler/view/layout triples, without Vulkan
     * structure padding or pointers to the caller's temporary write arrays. */
    uint64_t words[21];
} AndroidDescriptorContentsKey;

static struct {
    AndroidDescriptorContentsKey key;
    VkCommandBuffer command;
    VkPipelineBindPoint point;
    VkPipelineLayout layout;
    VkDescriptorSetLayout set_layout;
    uint32_t generation, count, misses, bypass;
} s_android_descriptor_contents_last;

static struct {
    uint64_t requests, matches, skipped, writes_skipped, unsupported, bypassed;
} s_android_descriptor_contents_stats;

#if VK_USE_64_BIT_PTR_DEFINES
#define ANDROID_DESCRIPTOR_WORD(handle) ((uint64_t)(uintptr_t)(handle))
#else
#define ANDROID_DESCRIPTOR_WORD(handle) ((uint64_t)(handle))
#endif

static int android_descriptor_contents_key(uint32_t index, uint32_t count,
    const VkWriteDescriptorSet *writes, AndroidDescriptorContentsKey *key)
{
    if (index || (count != 7 && count != 1) || !writes) return 0;
    for (uint32_t i = 0; i < count; i++) {
        const VkWriteDescriptorSet *w = &writes[i];
        if (w->sType != VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET || w->pNext
            || w->dstBinding != i || w->dstArrayElement || w->descriptorCount != 1)
            return 0;
        uint64_t *words = key->words + i * 3;
        if (count == 7 && (i < 2 || i == 6)) {
            if (w->descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || !w->pBufferInfo)
                return 0;
            words[0] = ANDROID_DESCRIPTOR_WORD(w->pBufferInfo->buffer);
            words[1] = w->pBufferInfo->offset;
            words[2] = w->pBufferInfo->range;
        } else {
            if (w->descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || !w->pImageInfo)
                return 0;
            words[0] = ANDROID_DESCRIPTOR_WORD(w->pImageInfo->sampler);
            words[1] = ANDROID_DESCRIPTOR_WORD(w->pImageInfo->imageView);
            words[2] = (uint64_t)w->pImageInfo->imageLayout;
        }
    }
    return 1;
}
#undef ANDROID_DESCRIPTOR_WORD

static void android_descriptor_contents_invalidate(void)
{
    s_android_descriptor_contents_last.count = 0;
}

static void android_descriptor_contents_reset(void)
{
    android_descriptor_contents_invalidate();
    s_android_descriptor_contents_last.misses = 0;
    s_android_descriptor_contents_last.bypass = 0;
}

static void android_descriptor_contents_miss(void)
{
    if (++s_android_descriptor_contents_last.misses >= 8) {
        /* After eight misses, try two consecutive requests per ten. A single
         * isolated probe could never discover a consecutive-repeat run. */
        s_android_descriptor_contents_last.misses = 6;
        s_android_descriptor_contents_last.bypass = 8;
    }
}

static int android_descriptor_contents_matches(VkCommandBuffer command,
    VkPipelineBindPoint point, VkPipelineLayout layout, VkDescriptorSetLayout set_layout,
    uint32_t generation, uint32_t count, const AndroidDescriptorContentsKey *key)
{
    return s_android_descriptor_contents_last.count == count
        && s_android_descriptor_contents_last.command == command
        && s_android_descriptor_contents_last.point == point
        && s_android_descriptor_contents_last.layout == layout
        && s_android_descriptor_contents_last.set_layout == set_layout
        && s_android_descriptor_contents_last.generation == generation
        && !memcmp(s_android_descriptor_contents_last.key.words, key->words,
                   (size_t)count * 3 * sizeof key->words[0]);
}

static void android_descriptor_contents_remember(VkCommandBuffer command,
    VkPipelineBindPoint point, VkPipelineLayout layout, VkDescriptorSetLayout set_layout,
    uint32_t generation, uint32_t count, const AndroidDescriptorContentsKey *key)
{
    memcpy(s_android_descriptor_contents_last.key.words, key->words,
           (size_t)count * 3 * sizeof key->words[0]);
    s_android_descriptor_contents_last.command = command;
    s_android_descriptor_contents_last.point = point;
    s_android_descriptor_contents_last.layout = layout;
    s_android_descriptor_contents_last.set_layout = set_layout;
    s_android_descriptor_contents_last.generation = generation;
    s_android_descriptor_contents_last.count = count;
}
#endif
