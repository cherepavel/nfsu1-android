/* cc -std=c11 -Wall -Wextra -Werror -Wno-missing-field-initializers
 * -fsanitize=address,undefined -Ithird_party/SDL/src/video/khronos
 * tests/vulkan_pass_barrier.c -o /tmp/nfsu-pass-barrier-test
 * Also build with -D__ANDROID__ to exercise live Android policy. */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#define VK_MAX_SURF 4
static struct { int dirty; } s_surf[VK_MAX_SURF];
static struct {
    VkQueryPool gpu_queries;
    uint32_t gpu_barriers, gpu_pass_barriers;
} frame, *s_f = &frame;
static VkCommandBuffer s_cb;
static unsigned calls;
static VkPipelineStageFlags src_stage, dst_stage;
static VkMemoryBarrier memory;
#ifdef __ANDROID__
#define PROP_VALUE_MAX 92
static uint32_t s_frame;
static const char *property = "";
static unsigned reads;
static int test_property_get(const char *name, char *value)
{
    assert(!strcmp(name, "debug.nfsu.pass_barrier"));
    reads++;
    strcpy(value, property);
    return (int)strlen(property);
}
#define __system_property_get test_property_get
#define NFS_LOG_INFO(...) ((void)0)
#endif
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer command,
    VkPipelineStageFlags src, VkPipelineStageFlags dst, VkDependencyFlags flags,
    uint32_t count, const VkMemoryBarrier *barriers,
    uint32_t buffer_count, const VkBufferMemoryBarrier *buffers,
    uint32_t image_count, const VkImageMemoryBarrier *images)
{
    assert(command == s_cb && !flags && count == 1);
    assert(!buffer_count && !buffers && !image_count && !images);
    assert(barriers->sType == VK_STRUCTURE_TYPE_MEMORY_BARRIER && !barriers->pNext);
    calls++;
    src_stage = src; dst_stage = dst; memory = *barriers;
}
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_barrier.h"
static void dirty(void)
{
    for (int i = 0; i < VK_MAX_SURF; i++) s_surf[i].dirty = 1;
}
static void visible(void)
{
    for (int i = 0; i < VK_MAX_SURF; i++) assert(!s_surf[i].dirty);
}
static void full(void)
{
    assert(src_stage == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    assert(dst_stage == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    assert(memory.srcAccessMask == VK_ACCESS_MEMORY_WRITE_BIT);
    assert(memory.dstAccessMask == (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT));
    visible();
}
int main(void)
{
    frame.gpu_queries = (VkQueryPool)(uintptr_t)1;
    dirty(); barrier_all(); full();
    assert(calls == 1 && frame.gpu_barriers == 1 && !frame.gpu_pass_barriers);
    dirty(); barrier_before_rendering(); full(); /* Unset is the original path. */
    assert(calls == 2 && frame.gpu_barriers == 2 && !frame.gpu_pass_barriers);
#ifdef __ANDROID__
    assert(reads == 1);
    property = "1";
    s_frame = 299;
    barrier_before_rendering(); full();
    assert(reads == 1); /* No property lookup on every pass. */
    s_frame = 300;
    dirty(); barrier_before_rendering(); visible();
    assert(reads == 2 && frame.gpu_barriers == 3 && frame.gpu_pass_barriers == 1);
    /* The producer scope includes transfers, late depth/stencil stores,
     * previous command buffers and sampled-image reads (WAR execution).
     * Neither the fragment nor early/late depth consumer may be omitted. */
    assert(src_stage == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    assert(dst_stage == (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
        | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT));
    assert(memory.srcAccessMask == VK_ACCESS_MEMORY_WRITE_BIT);
    assert(memory.dstAccessMask == (VK_ACCESS_SHADER_READ_BIT
        | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT));
    dirty(); barrier_all(); full(); /* Transfers/readback keep their full scope. */
    assert(frame.gpu_barriers == 4 && frame.gpu_pass_barriers == 1);
    s_f->gpu_queries = VK_NULL_HANDLE;
    barrier_before_rendering();
    assert(frame.gpu_pass_barriers == 1); /* No profiling counters when disabled. */
    property = "0"; s_frame = 600;
    dirty(); barrier_before_rendering(); full();
    assert(reads == 3);
    property = "invalid"; s_frame = 900;
    barrier_before_rendering(); full();
    assert(reads == 4);
    property = ""; s_frame = 1200;
    barrier_before_rendering(); full();
    assert(reads == 5);
#endif
    puts("Pass barrier: original default, dependency scopes, dirty visibility, live rollback, counters: PASS");
}
