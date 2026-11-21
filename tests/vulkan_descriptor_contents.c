#define TEST_DESCRIPTOR_CONTENTS 1
#include "vulkan_descriptor_reuse.c"

static void request(unsigned present, unsigned changed, unsigned poison,
    VkCommandBuffer command, VkPipelineLayout layout, VkPipelineBindPoint point, int extended)
{
    VkWriteDescriptorSet writes[7];
    memset(writes, 0, sizeof writes);
    memset(expected_buffers, (int)poison, sizeof expected_buffers);
    memset(expected_images, (int)poison, sizeof expected_images);
    for (unsigned i = 0; i < 3; i++) {
        expected_buffers[i].buffer = (VkBuffer)(uintptr_t)(10 + i);
        expected_buffers[i].offset = (VkDeviceSize)i * 256;
        expected_buffers[i].range = 64;
    }
    for (unsigned i = 0; i < 4; i++) {
        expected_images[i].sampler = (VkSampler)(uintptr_t)(20 + i);
        expected_images[i].imageView = (VkImageView)(uintptr_t)(30 + i);
        expected_images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    if (changed < 9) {
        VkDescriptorBufferInfo *b = &expected_buffers[changed / 3];
        if (changed % 3 == 0) b->buffer = (VkBuffer)(uintptr_t)99;
        else if (changed % 3 == 1) b->offset += 256;
        else b->range += 16;
    } else if (changed < 21) {
        unsigned field = changed - 9;
        VkDescriptorImageInfo *image = &expected_images[field / 3];
        if (field % 3 == 0) image->sampler = (VkSampler)(uintptr_t)99;
        else if (field % 3 == 1) image->imageView = (VkImageView)(uintptr_t)99;
        else image->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    unsigned count = present ? 1 : 7;
    for (unsigned i = 0; i < count; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = (VkDescriptorSet)(uintptr_t)(100 + poison); /* Overwritten. */
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].pTexelBufferView = (const VkBufferView *)(uintptr_t)1; /* Ignored. */
        if (!present && (i < 2 || i == 6)) {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[i].pBufferInfo = &expected_buffers[i == 6 ? 2 : i];
            writes[i].pImageInfo = (const VkDescriptorImageInfo *)(uintptr_t)1;
        } else {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &expected_images[present ? 0 : i - 2];
            writes[i].pBufferInfo = (const VkDescriptorBufferInfo *)(uintptr_t)1;
        }
    }
    if (extended) writes[0].pNext = &extended; /* Mock-only opaque extension. */
    android_push_descriptors(command, point, layout, 0, count, writes);
    MockSet *bound = &sets[(uintptr_t)last_set];
    /* Also check every skipped bind against what the simulated GPU still sees.
     * Compare semantic fields: Vulkan image-info padding is intentionally varied. */
    for (unsigned i = 0; i < (present ? 1u : 4u); i++) {
        assert(bound->images[i].sampler == expected_images[i].sampler);
        assert(bound->images[i].imageView == expected_images[i].imageView);
        assert(bound->images[i].imageLayout == expected_images[i].imageLayout);
    }
    if (!present)
        assert(!memcmp(bound->buffers, expected_buffers, sizeof expected_buffers));
}
#define REQUEST(p, change, poison) request(p, change, poison, (VkCommandBuffer)(uintptr_t)1, \
    (VkPipelineLayout)(uintptr_t)1, VK_PIPELINE_BIND_POINT_GRAPHICS, 0)

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--selector")) {
        assert(android_descriptor_contents_requested() == atoi(argv[2])); return 0;
    }
    assert(argc == 1);
    s_android_descriptor_contents = 1;
    complete_and_begin(0);
    /* All 21 semantic descriptor fields participate in equality. */
    for (unsigned field = 0; field < 21; field++) {
        android_descriptor_contents_invalidate();
        unsigned before = binds;
        REQUEST(0, 21, 0); assert(binds == before + 1);
        REQUEST(0, 21, 0xa5); assert(binds == before + 1);
        REQUEST(0, field, 0); assert(binds == before + 2);
        REQUEST(0, field, 0x5a); assert(binds == before + 2);
        REQUEST(0, 21, 0); assert(binds == before + 3); /* A/B/A is a miss. */
    }
    /* Caller pointers/padding are not cache keys, both layouts are exact. */
    for (unsigned field = 9; field < 12; field++) {
        unsigned before = binds;
        REQUEST(1, 21, 0); REQUEST(1, 21, 0x7f);
        assert(binds == before + 1);
        REQUEST(1, field, 0); assert(binds == before + 2);
    }
    /* Unsupported shapes are rejected without reading irrelevant pointers. */
    AndroidDescriptorContentsKey key;
    VkWriteDescriptorSet invalid = {0};
    assert(!android_descriptor_contents_key(0, 0, NULL, &key));
    assert(!android_descriptor_contents_key(0, 8, &invalid, &key));
    assert(!android_descriptor_contents_key(1, 1, &invalid, &key));
    invalid.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    invalid.descriptorCount = 1;
    invalid.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    assert(!android_descriptor_contents_key(0, 1, &invalid, &key));
    invalid.pImageInfo = expected_images; invalid.dstArrayElement = 1;
    assert(!android_descriptor_contents_key(0, 1, &invalid, &key));
    invalid.dstArrayElement = 0; invalid.dstBinding = 1;
    assert(!android_descriptor_contents_key(0, 1, &invalid, &key));
    invalid.dstBinding = 0; invalid.descriptorCount = 2;
    assert(!android_descriptor_contents_key(0, 1, &invalid, &key));
    /* Command handle, recording generation, pipeline layout and bind point. */
    REQUEST(0, 21, 0);
    unsigned before = binds;
    REQUEST(0, 21, 0); assert(binds == before);
    s_cb_gen++; REQUEST(0, 21, 0); assert(binds == ++before);
    request(0, 21, 0, (VkCommandBuffer)(uintptr_t)2, (VkPipelineLayout)(uintptr_t)1,
        VK_PIPELINE_BIND_POINT_GRAPHICS, 0); assert(binds == ++before);
    request(0, 21, 0, (VkCommandBuffer)(uintptr_t)2, (VkPipelineLayout)(uintptr_t)2,
        VK_PIPELINE_BIND_POINT_GRAPHICS, 0); assert(binds == ++before);
    request(0, 21, 0, (VkCommandBuffer)(uintptr_t)2, (VkPipelineLayout)(uintptr_t)2,
        VK_PIPELINE_BIND_POINT_COMPUTE, 0); assert(binds == ++before);
    assert(!android_descriptor_contents_matches((VkCommandBuffer)(uintptr_t)2,
        VK_PIPELINE_BIND_POINT_COMPUTE, (VkPipelineLayout)(uintptr_t)2,
        s_pp_dsl, s_cb_gen, 7, &s_android_descriptor_contents_last.key));
    REQUEST(0, 21, 0); before = binds;
    request(0, 21, 0, (VkCommandBuffer)(uintptr_t)1, (VkPipelineLayout)(uintptr_t)1,
        VK_PIPELINE_BIND_POINT_GRAPHICS, 1); assert(binds == ++before);
    REQUEST(0, 21, 0); assert(binds == ++before);
    assert(s_android_descriptor_contents_stats.unsupported == 1);
    /* Slot/fence/pool resets invalidate even if all handles/data repeat. */
    for (unsigned f = 0; f < 8; f++) {
        complete_and_begin(f % 2); before = binds;
        REQUEST(0, 21, 0); REQUEST(0, 21, 0xa5); assert(binds == before + 1);
        draw(100 + f, 1, 1); REQUEST(0, 21, 0); assert(binds == before + 3);
    }
    /* Changing contents enters bounded backoff; two consecutive probes can
     * rediscover repeats. Bypassed requests still issue the original calls. */
    complete_and_begin(0); before = binds;
    uint64_t skipped_before = s_android_descriptor_contents_stats.skipped;
    for (unsigned i = 0; i < 100; i++) draw(1000 + i, 0, 1);
    assert(binds == before + 100 && s_android_descriptor_contents_stats.skipped == skipped_before);
    assert(s_android_descriptor_contents_stats.bypassed >= 64);
    for (unsigned i = 0; i < 20; i++) draw(5000, 0, 1);
    assert(s_android_descriptor_contents_stats.skipped > skipped_before);
    before = binds;
    for (unsigned i = 0; i < 9000; i++) draw(5000, 0, 1);
    assert(binds == before); /* Repeats do not consume the 8192-set budget. */
    /* Measure-only and rollback preserve all original calls. */
    for (int mode = 0; mode <= 2; mode += 2) {
        s_android_descriptor_contents = mode;
        complete_and_begin(0); before = binds;
        REQUEST(0, 21, 0); REQUEST(0, 21, 0xa5); assert(binds == before + 2);
    }
    assert(s_android_descriptor_contents_stats.skipped > 50 && binds == updates);
    complete_and_begin(0); complete_and_begin(1);
    for (unsigned f = 0; f < 2; f++) {
        AndroidDescriptorReuse *reuse = frames[f].descriptor_reuse;
        if (reuse) { free(reuse->lists[0].sets); free(reuse->lists[1].sets); free(reuse); }
    }
    puts("Descriptor contents: all binding fields, caller padding/pointers, bound contents, layouts, command generations, fence slots, unsupported writes, count-only and rollback: PASS");
}
