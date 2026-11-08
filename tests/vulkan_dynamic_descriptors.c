/* cc -std=c11 -Wall -Wextra -Werror -Wno-missing-field-initializers
 * -fsanitize=address,undefined -Ithird_party/SDL/src/video/khronos
 * tests/vulkan_dynamic_descriptors.c -o /tmp/nfsu-dynamic-desc-test */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RING_BYTES (48u << 20)
#define NFS_LOG_INFO(...) ((void)0)
#define NFS_LOG_FATAL(...) ((void)0)
typedef struct { VkDescriptorPool descriptors; struct AndroidDescriptorCache *descriptor_cache; } Frame;
static Frame frames[2], *s_f = &frames[0];
static VkDevice s_dev;
static VkDescriptorSetLayout s_dsl;
static VkDeviceSize s_ubo_align = 64;
static uint32_t s_frame;
static int s_android_dynamic_descriptors = 1;
static unsigned allocations, updates, binds, fallback;
static VkDescriptorSet last_set;
static uint32_t last_offsets[3];
static VkDescriptorBufferInfo last_buffers[3];
static void android_descriptor_pool_ready(void) { s_f->descriptors = (VkDescriptorPool)(uintptr_t)1; }
static void VKAPI_PTR android_push_descriptors(VkCommandBuffer command, VkPipelineBindPoint point,
    VkPipelineLayout layout, uint32_t index, uint32_t count, const VkWriteDescriptorSet *writes)
{
    (void)command; (void)point; (void)layout; (void)index; (void)writes;
    assert(count == 1); fallback++;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice device,
    const VkDescriptorSetAllocateInfo *info, VkDescriptorSet *set)
{
    (void)device; assert(info->descriptorSetCount == 1 && info->descriptorPool == s_f->descriptors);
    *set = (VkDescriptorSet)(uintptr_t)++allocations; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice device, uint32_t count,
    const VkWriteDescriptorSet *writes, uint32_t copies, const VkCopyDescriptorSet *copy)
{
    (void)device; (void)copy; assert(count == 7 && !copies); updates++;
    for (unsigned i=0;i<7;i++) {
        assert(writes[i].dstBinding == i && writes[i].descriptorCount == 1);
        if (i<2 || i==6) {
            assert(writes[i].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
            assert(writes[i].pBufferInfo->offset == 0);
            last_buffers[i==6 ? 2:i] = *writes[i].pBufferInfo;
        } else assert(writes[i].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    }
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer command, VkPipelineBindPoint point,
    VkPipelineLayout layout, uint32_t first, uint32_t count, const VkDescriptorSet *sets,
    uint32_t dynamic_count, const uint32_t *offsets)
{
    (void)command; (void)point; (void)layout; assert(first == 0 && count == 1 && dynamic_count == 3);
    last_set = *sets; memcpy(last_offsets,offsets,sizeof last_offsets); binds++;
}
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_dynamic_descriptors.h"
static void draw(VkWriteDescriptorSet *writes)
{
    android_dynamic_descriptors(VK_NULL_HANDLE,VK_PIPELINE_BIND_POINT_GRAPHICS,VK_NULL_HANDLE,0,7,writes);
}
int main(void)
{
    VkDescriptorBufferInfo buffers[3] = {
        {(VkBuffer)(uintptr_t)10,64,256}, {(VkBuffer)(uintptr_t)10,512,128},
        {(VkBuffer)(uintptr_t)10,1024,15360}
    };
    VkDescriptorImageInfo images[4] = {0};
    VkWriteDescriptorSet writes[7] = {0};
    for (unsigned i=0;i<7;i++) {
        writes[i].dstBinding=i; writes[i].descriptorCount=1;
        if (i<2 || i==6) {
            writes[i].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[i].pBufferInfo=&buffers[i==6 ? 2:i];
        } else {
            images[i-2].sampler=(VkSampler)(uintptr_t)20;
            images[i-2].imageView=(VkImageView)(uintptr_t)(30+i);
            images[i-2].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
            writes[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo=&images[i-2];
        }
    }
    draw(writes); VkDescriptorSet first=last_set;
    assert(allocations==1 && updates==1 && binds==1 && !s_android_descriptor_hits);
    for (unsigned i=0;i<3;i++) {
        assert(last_offsets[i]==buffers[i].offset);
        assert(last_buffers[i].range==buffers[i].range && last_buffers[i].buffer==buffers[i].buffer);
    }
    buffers[0].offset+=64; buffers[1].offset+=256; buffers[2].offset+=512;
    draw(writes);
    assert(last_set==first && allocations==1 && updates==1 && binds==2 && s_android_descriptor_hits==1);
    for (unsigned i=0;i<3;i++) assert(last_offsets[i]==buffers[i].offset);
    images[2].imageView=(VkImageView)(uintptr_t)99; draw(writes); assert(allocations==2);
    images[2].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; draw(writes); assert(allocations==3);
    images[2].sampler=(VkSampler)(uintptr_t)999; draw(writes); assert(allocations==4);
    buffers[2].range-=3072; draw(writes); assert(allocations==5); /* truncated ring range matters */
    buffers[0].buffer=(VkBuffer)(uintptr_t)11; draw(writes); assert(allocations==6);
    VkWriteDescriptorSet reversed[7];
    for (unsigned i=0;i<7;i++) reversed[i]=writes[6-i];
    draw(reversed); assert(allocations==6);
    for (unsigned i=0;i<3;i++) assert(last_offsets[i]==buffers[i].offset);
    AndroidDescriptorKey key; uint32_t offsets[3];
    buffers[0].offset++; assert(!android_descriptor_key(writes,64,&key,offsets)); buffers[0].offset--;
    buffers[2].range=VK_WHOLE_SIZE; assert(!android_descriptor_key(writes,64,&key,offsets));
    buffers[2].range=RING_BYTES; assert(!android_descriptor_key(writes,64,&key,offsets));
    buffers[2].range=3072;
    reversed[0]=writes[0]; assert(!android_descriptor_key(reversed,64,&key,offsets));
    s_f=&frames[1]; draw(writes); assert(allocations==7); /* no sharing with in-flight frame */
    s_f=&frames[0]; android_descriptor_cache_reset(s_f); draw(writes); assert(allocations==8);
    /* Exercise collisions and a full CPU cache. Uncached misses keep the
     * existing bound sets immutable and do not overwrite a live entry. */
    while (s_f->descriptor_cache->count<ANDROID_DESCRIPTOR_CACHE_ENTRIES) {
        images[0].imageView=(VkImageView)(uintptr_t)(10000+s_f->descriptor_cache->count);
        draw(writes);
    }
    images[0].imageView=(VkImageView)(uintptr_t)999999;
    unsigned before=allocations; draw(writes); draw(writes);
    assert(allocations==before+2 && s_f->descriptor_cache->count==ANDROID_DESCRIPTOR_CACHE_ENTRIES);
    android_descriptor_cache_reset(s_f);
    assert(!s_f->descriptor_cache->count);
    for (unsigned i=0;i<ANDROID_DESCRIPTOR_CACHE_SLOTS;i++) assert(!s_f->descriptor_cache->slots[i]);
    android_dynamic_descriptors(VK_NULL_HANDLE,VK_PIPELINE_BIND_POINT_GRAPHICS,VK_NULL_HANDLE,0,1,writes);
    assert(fallback==1);
    free(frames[0].descriptor_cache); free(frames[1].descriptor_cache);
    puts("Dynamic descriptors: effective addresses/ranges, canonical order, resource identity, frame isolation, reset, collisions/full-cache fallback: PASS");
}
