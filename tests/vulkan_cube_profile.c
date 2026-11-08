/* Production timestamp helpers against a mock Vulkan query pool. */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define GPU_PASS_LIMIT 64
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_cube_profile.h"

static VkCommandBuffer command = (VkCommandBuffer)(uintptr_t)1;
static VkQueryPool pool = (VkQueryPool)(uintptr_t)2;
static VkDevice device = (VkDevice)(uintptr_t)3;
static uint32_t writes[GPU_CUBE_PROFILE_LIMIT * 3u], nwrites, reads;
static uint64_t query_ticks[GPU_CUBE_PROFILE_LIMIT][3];
static VkResult query_result[GPU_CUBE_PROFILE_LIMIT];
static int inside_rendering, frame_fence_complete;

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer cb,
    VkPipelineStageFlagBits stage, VkQueryPool queries, uint32_t query)
{
    assert(cb == command && queries == pool && !inside_rendering);
    assert(stage == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    assert(query >= GPU_CUBE_QUERY_BASE && query < GPU_CUBE_QUERY_COUNT);
    assert(nwrites < GPU_CUBE_PROFILE_LIMIT * 3u);
    writes[nwrites++] = query;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice dev, VkQueryPool queries,
    uint32_t first, uint32_t count, size_t bytes, void *data, VkDeviceSize stride,
    VkQueryResultFlags flags)
{
    assert(dev == device && queries == pool && frame_fence_complete);
    assert(first >= GPU_CUBE_QUERY_BASE && (first - GPU_CUBE_QUERY_BASE) % 3u == 0);
    uint32_t slot = (first - GPU_CUBE_QUERY_BASE) / 3u;
    assert(slot < GPU_CUBE_PROFILE_LIMIT && count == 3);
    assert(bytes == sizeof query_ticks[slot] && stride == sizeof(uint64_t));
    assert(flags == VK_QUERY_RESULT_64_BIT); /* no WAIT or partial results */
    reads++;
    if (query_result[slot] != VK_SUCCESS) return query_result[slot];
    memcpy(data, query_ticks[slot], bytes);
    return VK_SUCCESS;
}

static uint32_t record(VkCubeProfile *p)
{
    uint32_t slot = gpu_cube_profile_begin(p, command, pool, 1, 1);
    assert(slot != UINT32_MAX);
    gpu_cube_profile_stamp(p, command, pool, slot, 1);
    gpu_cube_profile_stamp(p, command, pool, slot, 2);
    return slot;
}

int main(void)
{
    VkCubeProfile p = {0};
    assert(gpu_timestamp_query_count(0, 1) == 3);
    assert(gpu_timestamp_query_count(2, 1) == 3);
    assert(gpu_timestamp_query_count(1, 0) == 131);
    assert(gpu_timestamp_query_count(1, 1) == 155);
    assert(gpu_cube_profile_begin(&p, command, pool, 0, 1) == UINT32_MAX);
    assert(gpu_cube_profile_begin(&p, command, pool, 2, 1) == UINT32_MAX);
    assert(gpu_cube_profile_begin(&p, command, pool, 1, 0) == UINT32_MAX);
    assert(gpu_cube_profile_begin(&p, command, VK_NULL_HANDLE, 1, 1) == UINT32_MAX);
    assert(!p.count && !p.overflow && !nwrites);
    frame_fence_complete = 1;
    for (uint32_t i = 0; i < GPU_CUBE_PROFILE_LIMIT; i++) {
        assert(record(&p) == i);
        query_ticks[i][0] = i * 100;
        query_ticks[i][1] = i * 100 + 3;
        query_ticks[i][2] = i * 100 + 5;
    }
    assert(gpu_cube_profile_begin(&p, command, pool, 1, 1) == UINT32_MAX);
    assert(p.count == GPU_CUBE_PROFILE_LIMIT && p.overflow == 1);
    assert(nwrites == GPU_CUBE_PROFILE_LIMIT * 3u);
    for (uint32_t i = 0; i < nwrites; i++) assert(writes[i] == GPU_CUBE_QUERY_BASE + i);
    gpu_cube_profile_stamp(&p, command, pool, UINT32_MAX, 1);
    gpu_cube_profile_stamp(&p, command, pool, GPU_CUBE_PROFILE_LIMIT, 2);
    VkCubeProfileTimes t = gpu_cube_profile_read(&p, device, pool, 64, 1000000.0f);
    assert(t.timed == 8 && !t.missing && t.copy_ms == 24 && t.mip_ms == 16);
    query_result[2] = VK_NOT_READY; query_result[3] = VK_ERROR_DEVICE_LOST;
    t = gpu_cube_profile_read(&p, device, pool, 64, 1000000.0f);
    assert(t.timed == 6 && t.missing == 2 && t.copy_ms == 18 && t.mip_ms == 12);

    p.faces = 48; p.mip_blits = 56;
    gpu_cube_profile_reset(&p);
    assert(!p.count && !p.overflow && !p.faces && !p.mip_blits);
    for (uint32_t i = 0; i < GPU_CUBE_PROFILE_LIMIT; i++) assert(!p.stamps[i]);
    nwrites = 0; memset(query_result, 0, sizeof query_result);
    uint32_t slot = gpu_cube_profile_begin(&p, command, pool, 1, 1);
    gpu_cube_profile_stamp(&p, command, pool, slot, 2); /* missing midpoint */
    gpu_cube_profile_stamp(&p, command, pool, slot, 0);
    assert(nwrites == 1);
    gpu_cube_profile_stamp(&p, command, pool, slot, 1);
    gpu_cube_profile_stamp(&p, command, pool, slot, 1); /* duplicate ignored */
    command = (VkCommandBuffer)(uintptr_t)4; /* same frame/pool, new CB */
    gpu_cube_profile_stamp(&p, command, pool, slot, 2);
    gpu_cube_profile_stamp(&p, command, pool, slot, 2);
    assert(nwrites == 3 && p.stamps[slot] == 7);
    query_ticks[0][0] = 250; query_ticks[0][1] = 4; query_ticks[0][2] = 9;
    t = gpu_cube_profile_read(&p, device, pool, 8, 1000000.0f);
    assert(t.timed == 1 && !t.missing && t.copy_ms == 10 && t.mip_ms == 5);
    uint32_t before = reads;
    t = gpu_cube_profile_read(&p, device, pool, 0, 1000000.0f);
    assert(!t.timed && t.missing == 1 && reads == before);
    t = gpu_cube_profile_read(&p, device, pool, 64, 0);
    assert(!t.timed && t.missing == 1 && reads == before);
    query_ticks[0][0] = UINT64_MAX - 1; query_ticks[0][1] = 2; query_ticks[0][2] = 5;
    t = gpu_cube_profile_read(&p, device, pool, 64, 1000000.0f);
    assert(t.timed == 1 && t.copy_ms == 4 && t.mip_ms == 3);

    /* An unfinished group cannot read stale results from the previous frame. */
    gpu_cube_profile_reset(&p); nwrites = 0;
    slot = gpu_cube_profile_begin(&p, command, pool, 1, 1);
    before = reads;
    t = gpu_cube_profile_read(&p, device, pool, 64, 1.0f);
    assert(!t.timed && t.missing == 1 && reads == before);
    gpu_cube_profile_stamp(&p, command, pool, slot, 1);
    gpu_cube_profile_stamp(&p, command, pool, slot, 2);
    assert(gpu_cube_profile_begin(&p, command, pool, 1, 1) == 1);
    t = gpu_cube_profile_read(&p, device, pool, 64, 1000000.0f);
    assert(t.timed == 1 && t.missing == 1 && t.copy_ms == 4 && t.mip_ms == 3);
    puts("Cube GPU profile: query ranges, modes, coverage, timestamp wrap and reuse: PASS");
    return 0;
}
