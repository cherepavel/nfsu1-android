/* Actual production command selector against a mock Vulkan command buffer.
 * cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined
 * -Ithird_party/SDL/src/video/khronos tests/vulkan_dynamic_state.c
 * -o /tmp/nfsu-dynamic-state-test */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static VkCommandBuffer s_cb = (VkCommandBuffer)(uintptr_t)1;
static int s_dyn_valid;
static struct { uint32_t bcolor, smask, swrite, sref; } s_dyn;
static float driver_color[4];
static uint32_t driver_compare, driver_write, driver_reference, emitted;

static void argb_vec4(uint32_t color, float out[4])
{
    out[0] = ((color >> 16) & 255) / 255.0f;
    out[1] = ((color >> 8) & 255) / 255.0f;
    out[2] = (color & 255) / 255.0f;
    out[3] = (color >> 24) / 255.0f;
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(VkCommandBuffer cb, const float values[4])
{
    assert(cb == s_cb && !(emitted & 1));
    emitted |= 1;
    memcpy(driver_color, values, sizeof driver_color);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilCompareMask(VkCommandBuffer cb, VkStencilFaceFlags faces, uint32_t value)
{
    assert(cb == s_cb && faces == VK_STENCIL_FACE_FRONT_AND_BACK && !(emitted & 2));
    emitted |= 2; driver_compare = value;
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilWriteMask(VkCommandBuffer cb, VkStencilFaceFlags faces, uint32_t value)
{
    assert(cb == s_cb && faces == VK_STENCIL_FACE_FRONT_AND_BACK && !(emitted & 4));
    emitted |= 4; driver_write = value;
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilReference(VkCommandBuffer cb, VkStencilFaceFlags faces, uint32_t value)
{
    assert(cb == s_cb && faces == VK_STENCIL_FACE_FRONT_AND_BACK && !(emitted & 8));
    emitted |= 8; driver_reference = value;
}
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_dynamic_state.h"

static uint32_t r[0x400 / 4];
static VkDynamicStateWrites apply(int cache)
{
    float color[4];
    emitted = 0;
    VkDynamicStateWrites w = android_apply_dynamic(r, cache);
    assert(w.emitted == emitted && s_dyn_valid);
    argb_vec4(r[0x34C / 4], color);
    assert(!memcmp(color, driver_color, sizeof color));
    assert(driver_compare == (r[0x36C / 4] & 255));
    assert(driver_write == (r[0x360 / 4] & 255));
    assert(driver_reference == (r[0x368 / 4] & 255));
    assert((w.needed & w.emitted) == w.needed);
    return w;
}

int main(void)
{
    assert(apply(1).emitted == 15); /* first zero values are still defined */
    r[0x34C / 4] = 0x12345678;
    r[0x36C / 4] = 0xFF;
    r[0x360 / 4] = 0x81;
    r[0x368 / 4] = 0x23;
    assert(apply(1).emitted == 15);
    assert(apply(1).emitted == 0);
    r[0x34C / 4] ^= 0x100;
    assert(apply(1).emitted == 1);
    r[0x36C / 4] ^= 1;
    assert(apply(1).emitted == 2);
    r[0x360 / 4] ^= 2;
    assert(apply(1).emitted == 4);
    r[0x368 / 4] ^= 4;
    assert(apply(1).emitted == 8);
    r[0x36C / 4] ^= 0x100;
    r[0x360 / 4] ^= 0x200;
    r[0x368 / 4] ^= 0x400;
    assert(apply(1).emitted == 0); /* guest upper bits never reach Vulkan */
    assert(apply(0).emitted == 15); /* original baseline really writes all */
    assert(apply(0).needed == 0);
    s_dyn_valid = 0; /* live mode switch, new CB, or static present pipeline */
    memset(driver_color, 0xCC, sizeof driver_color);
    driver_compare = driver_write = driver_reference = UINT32_MAX;
    assert(apply(1).emitted == 15);
    assert(apply(1).emitted == 0);
    s_cb = (VkCommandBuffer)(uintptr_t)2;
    s_dyn_valid = 0;
    assert(apply(1).emitted == 15);

    /* Check final mock-driver state against the original setters over
     * mixed changes, baseline/cached modes and command-buffer invalidations. */
    const unsigned regs[] = {0x34C/4, 0x36C/4, 0x360/4, 0x368/4};
    uint32_t random = 12345;
    for (unsigned i = 0; i < 100000; i++) {
        random = random * 1664525u + 1013904223u;
        if (i % 5) r[regs[(random >> 16) & 3]] ^= random;
        if (i % 101 == 0) s_dyn_valid = 0;
        VkDynamicStateWrites w = apply(i % 7 != 0);
        assert(dynamic_state_write_count(w.emitted) <= 4);
    }
    puts("Dynamic state: baseline/cached driver state, partial updates, masks and invalidation: PASS");
    return 0;
}
