/* cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined
 * -Ithird_party/SDL/src/video/khronos tests/vulkan_framebuffer_cache.c
 * -o /tmp/nfsu-framebuffer-cache-test */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    VkFramebuffer *framebuffers;
    uint32_t nframebuffers, framebuffer_cap;
} Frame;
static Frame frames[2], *s_f = &frames[0];
static VkDevice s_dev;
static unsigned creates, destroys;
static int fail_create;
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice device,
    const VkFramebufferCreateInfo *info, const VkAllocationCallbacks *allocator,
    VkFramebuffer *framebuffer)
{
    (void)device; (void)allocator;
    assert(info->layers == 1 && info->attachmentCount >= 1 && info->attachmentCount <= 2);
    if (fail_create) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *framebuffer = (VkFramebuffer)(uintptr_t)++creates;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice device,
    VkFramebuffer framebuffer, const VkAllocationCallbacks *allocator)
{
    (void)device; (void)allocator;
    assert(framebuffer);
    destroys++;
}
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_framebuffer_cache.h"
static void complete_frame(Frame *frame)
{
    for (unsigned i = 0; i < frame->nframebuffers; i++)
        vkDestroyFramebuffer(s_dev, frame->framebuffers[i], NULL);
    frame->nframebuffers = 0;
}
static void reset(void)
{
    for (unsigned i = 0; i < ANDROID_FRAMEBUFFER_CACHE_SIZE; i++)
        if (s_android_framebuffers[i].framebuffer)
            android_framebuffer_retire_view(s_android_framebuffers[i].views[0], NULL);
    for (unsigned i = 0; i < 2; i++) complete_frame(&frames[i]);
    assert(creates == destroys);
}
int main(void)
{
    VkImageView views[] = {(VkImageView)(uintptr_t)10, (VkImageView)(uintptr_t)20};
    VkFramebufferCreateInfo create = {0};
    create.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    create.renderPass = (VkRenderPass)(uintptr_t)1;
    create.pAttachments = views;
    create.attachmentCount = 2;
    create.width = 2133; create.height = 1200; create.layers = 1;
    VkFramebuffer first = android_framebuffer_get(&create, 1);
    assert(first && creates == 1 && !s_f->nframebuffers);
    s_f = &frames[1]; /* The next frame shares the immutable framebuffer. */
    assert(android_framebuffer_get(&create, 1) == first && creates == 1);
    complete_frame(&frames[0]);
    assert(!destroys); /* Ordinary fence reuse must keep cached entries. */
    create.width++;
    assert(android_framebuffer_get(&create, 1) != first);
    create.width--;
    create.renderPass = (VkRenderPass)(uintptr_t)2;
    assert(android_framebuffer_get(&create, 1) != first);
    create.renderPass = (VkRenderPass)(uintptr_t)1;
    create.attachmentCount = 1;
    assert(android_framebuffer_get(&create, 1) != first);
    /* Retiring depth invalidates only framebuffers that actually use it. */
    android_framebuffer_retire_view(views[1], s_f);
    assert(s_f->nframebuffers == 3 && !destroys);
    VkFramebuffer color = android_framebuffer_get(&create, 1);
    assert(creates == 4);
    android_framebuffer_retire_view(views[0], s_f);
    assert(s_f->nframebuffers == 4 && !destroys);
    /* A driver may recycle an image-view handle: it must get a fresh object. */
    assert(android_framebuffer_get(&create, 1) != color);
    complete_frame(s_f);
    assert(destroys == 4);
    reset();

    /* Off/on is live; uncached objects are still owned by the frame fence. */
    first = android_framebuffer_get(&create, 1);
    assert(android_framebuffer_get(&create, 0) != first);
    assert(android_framebuffer_get(&create, 0) != first);
    assert(s_f->nframebuffers == 2);
    assert(android_framebuffer_get(&create, 1) == first);
    complete_frame(s_f);
    assert(android_framebuffer_get(&create, 1) == first);
    reset();

    unsigned before = destroys;
    for (unsigned i = 0; i < ANDROID_FRAMEBUFFER_CACHE_SIZE; i++) {
        create.width = i + 1;
        assert(android_framebuffer_get(&create, 1));
    }
    create.width = 1;
    first = android_framebuffer_get(&create, 1); /* Make this the newest. */
    create.width = 1000;
    fail_create = 1;
    assert(!android_framebuffer_get(&create, 1));
    assert(!s_f->nframebuffers && destroys == before);
    fail_create = 0;
    assert(android_framebuffer_get(&create, 1));
    assert(s_f->nframebuffers == 1 && destroys == before);
    create.width = 1;
    assert(android_framebuffer_get(&create, 1) == first);
    complete_frame(s_f); /* Eviction destroys only after the frame completes. */
    assert(destroys == before + 1);
    reset();
    for (unsigned i = 0; i < 2; i++) free(frames[i].framebuffers);
    puts("Vulkan framebuffer cache: reuse, keys, view retirement, live bypass, fenced eviction: PASS");
}
