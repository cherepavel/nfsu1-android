#ifndef NV2A_VK_CUBE_PROFILE_H
#define NV2A_VK_CUBE_PROFILE_H
#include <stdint.h>
#include <string.h>

/* Disjoint from the three frame queries and the existing pass query range. */
#define GPU_CUBE_PROFILE_LIMIT 8u
#define GPU_CUBE_QUERY_BASE (3u + GPU_PASS_LIMIT * 2u)
#define GPU_CUBE_QUERY_COUNT (GPU_CUBE_QUERY_BASE + GPU_CUBE_PROFILE_LIMIT * 3u)

typedef struct {
    uint32_t count, overflow, faces, mip_blits;
    uint8_t stamps[GPU_CUBE_PROFILE_LIMIT];
} VkCubeProfile;

typedef struct {
    double copy_ms, mip_ms;
    uint32_t timed, missing;
} VkCubeProfileTimes;

static uint32_t gpu_timestamp_query_count(int profile, int cubes)
{
    return profile == 1 ? (cubes ? GPU_CUBE_QUERY_COUNT : GPU_CUBE_QUERY_BASE) : 3u;
}

static void gpu_cube_profile_reset(VkCubeProfile *p)
{
    memset(p, 0, sizeof *p);
}

/* Caller has ended rendering; the first stamp precedes the producer barrier.
 * These helpers introduce no submit, wait, allocation or query reset. */
static uint32_t gpu_cube_profile_begin(VkCubeProfile *p, VkCommandBuffer cb,
                                       VkQueryPool pool, int profile, int enabled)
{
    if (profile != 1 || !enabled || !pool) return UINT32_MAX;
    if (p->count >= GPU_CUBE_PROFILE_LIMIT) {
        p->overflow++;
        return UINT32_MAX;
    }
    uint32_t slot = p->count++;
    p->stamps[slot] = 1;
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool,
        GPU_CUBE_QUERY_BASE + slot * 3u);
    return slot;
}

static void gpu_cube_profile_stamp(VkCubeProfile *p, VkCommandBuffer cb,
                                   VkQueryPool pool, uint32_t slot, uint32_t phase)
{
    if (!pool || slot >= p->count || slot >= GPU_CUBE_PROFILE_LIMIT || phase < 1 || phase > 2)
        return;
    if (p->stamps[slot] != (phase == 1 ? 1u : 3u)) return;
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool,
        GPU_CUBE_QUERY_BASE + slot * 3u + phase);
    p->stamps[slot] |= (uint8_t)(1u << phase);
}

/* Read only after the frame's existing fence. Unavailable/unfinished groups
 * contribute coverage failures, never zero-time successful samples. */
static VkCubeProfileTimes gpu_cube_profile_read(const VkCubeProfile *p,
    VkDevice device, VkQueryPool pool, uint32_t bits, float period_ns)
{
    VkCubeProfileTimes result = {0};
    uint64_t mask = bits >= 64 ? UINT64_MAX : bits ? (1ull << bits) - 1 : 0;
    for (uint32_t i = 0; i < p->count && i < GPU_CUBE_PROFILE_LIMIT; i++) {
        uint64_t ticks[3];
        if (!pool || !bits || !(period_ns > 0) || p->stamps[i] != 7 ||
            vkGetQueryPoolResults(device, pool, GPU_CUBE_QUERY_BASE + i * 3u,
                3, sizeof ticks, ticks, sizeof ticks[0], VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
            result.missing++;
            continue;
        }
        result.copy_ms += ((ticks[1] - ticks[0]) & mask) * (double)period_ns / 1e6;
        result.mip_ms += ((ticks[2] - ticks[1]) & mask) * (double)period_ns / 1e6;
        result.timed++;
    }
    return result;
}
#endif
