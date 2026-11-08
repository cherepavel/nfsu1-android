/* The production pass/clear helpers with a mock driver.
 * cc -std=c11 -Wall -Wextra -Werror -Wno-missing-field-initializers
 * -fsanitize=address,undefined -Ithird_party/SDL/src/video/khronos
 * tests/vulkan_deferred_clear.c -o /tmp/nfsu-deferred-clear-test */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef struct {
    uint32_t va, pw, ph;
    uint32_t screen_seen, screen_frame;
    VkImageView view;
    int clear_pending;
    VkClearColorValue clear_value;
} VkSurf;
typedef struct {
    uint32_t pw, ph;
    VkImage image;
    VkImageView view;
    int clear_pending;
    VkClearDepthStencilValue clear_value;
} VkDepthBuf;
#define GPU_PASS_LIMIT 64
static struct {
    VkQueryPool gpu_queries;
    unsigned gpu_passes, gpu_pass_count;
    unsigned gpu_depth_passes;
    uint64_t gpu_depth_load_pixels, gpu_depth_store_pixels;
    int gpu_active_pass;
    struct {
        uint32_t target, width, height, clears, depth;
        uint64_t draws, indices, compact_eligible, compact_selected;
        const char *end_reason;
    } gpu_targets[GPU_PASS_LIMIT];
} frame, *s_f = &frame;
static VkCommandBuffer s_cb;
static int s_gpu_profile = 1;
static int s_in_render;
static VkSurf *s_rt;
static VkDepthBuf *s_rt_depth;
static unsigned begins, ends, barriers;
static unsigned depth_transfers;
static unsigned timestamps;
static uint32_t last_width, last_height;
static uint32_t s_frame;
#ifdef __ANDROID__
#define PROP_VALUE_MAX 92
static const char *property_value = "0";
static int test_property_get(const char *name, char *value)
{
    assert(!strcmp(name, "debug.nfsu.lazy_clear") || !strcmp(name, "debug.nfsu.unused_depth"));
    strcpy(value, property_value);
    return (int)strlen(value);
}
#define __system_property_get test_property_get
#define NFS_LOG_INFO(...) ((void)0)
#endif
static VkAttachmentLoadOp color_load, depth_load;
static VkClearColorValue color_value;
static VkClearDepthStencilValue depth_value;
static void end_rendering(void)
{
    if (s_in_render) ends++;
    s_in_render = 0;
    frame.gpu_active_pass = -1;
}
static void barrier_all(void) { barriers++; }
static void barrier_before_rendering(void) { barriers++; }
static void set_viewport(uint32_t w, uint32_t h) { assert(w && h); }
static void p_begin_rendering(VkCommandBuffer command, const VkRenderingInfo *info)
{
    (void)command;
    begins++;
    last_width = info->renderArea.extent.width;
    last_height = info->renderArea.extent.height;
    assert(info->pColorAttachments && info->layerCount == 1);
    color_load = info->pColorAttachments->loadOp;
    color_value = info->pColorAttachments->clearValue.color;
    depth_load = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    if (info->pDepthAttachment) {
        assert(info->pDepthAttachment == info->pStencilAttachment);
        depth_load = info->pDepthAttachment->loadOp;
        depth_value = info->pDepthAttachment->clearValue.depthStencil;
    }
}
VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer command,
    VkPipelineStageFlagBits stage, VkQueryPool pool, uint32_t query)
{
    (void)command; (void)stage; (void)pool;
    assert(query < (s_gpu_profile == 1 ? 3 + GPU_PASS_LIMIT * 2 : 3));
    timestamps++;
}
VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(VkCommandBuffer command,
    VkImage image, VkImageLayout layout, const VkClearDepthStencilValue *value,
    uint32_t count, const VkImageSubresourceRange *range)
{
    (void)command; (void)image;
    assert(layout == VK_IMAGE_LAYOUT_GENERAL && count == 1);
    assert(range->aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
    assert(range->levelCount == 1 && range->layerCount == 1);
    depth_value = *value;
    depth_transfers++;
}
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_unused_depth.h"
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_render_pass.h"
int main(void)
{
    VkSurf a = {.va=1, .pw=640, .ph=480}, b = {.va=2, .pw=640, .ph=480};
    VkDepthBuf depth = {.pw=640, .ph=480};
    VkClearRect rect = {{{0,0},{640,480}},0,1};
    VkClearAttachment clear[2] = {0};
    clear[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clear[0].clearValue.color.float32[0] = .75f;
    clear[0].clearValue.color.float32[3] = 1;
    clear[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    clear[1].clearValue.depthStencil = (VkClearDepthStencilValue){.25f,19};
    assert(!lazy_clears_enabled()); /* Host policy remains disabled. */
    assert(!unused_depth_enabled());
#ifdef __ANDROID__
    property_value = "1";
    assert(!lazy_clears_enabled());
    s_frame = 300;
    assert(lazy_clears_enabled());
    property_value = "0";
    s_frame = 600;
    assert(!lazy_clears_enabled());
#endif
    assert(defer_full_clear(&a,&depth,&rect,2,clear));
    assert(a.clear_pending && depth.clear_pending && !begins && !barriers);
    clear[0].clearValue.color.float32[0] = .5f;
    assert(defer_full_clear(&a,&depth,&rect,2,clear)); /* Latest complete clear wins. */
    frame.gpu_queries = (VkQueryPool)(uintptr_t)1;
    frame.gpu_targets[0].compact_eligible = frame.gpu_targets[0].compact_selected = 99;
    begin_rendering(&a,&depth);
    assert(!frame.gpu_targets[0].compact_eligible && !frame.gpu_targets[0].compact_selected);
    assert(begins == 1 && color_load == VK_ATTACHMENT_LOAD_OP_CLEAR && depth_load == VK_ATTACHMENT_LOAD_OP_CLEAR);
    assert(color_value.float32[0] == .5f && color_value.float32[3] == 1);
    assert(depth_value.depth == .25f && depth_value.stencil == 19);
    assert(frame.gpu_depth_passes == 1 && !frame.gpu_depth_load_pixels);
    assert(frame.gpu_depth_store_pixels == 640u * 480u);
    assert(!a.clear_pending && !depth.clear_pending && frame.gpu_targets[0].clears == 1);
    begin_rendering(&a,&depth);
    assert(begins == 1); /* Ordinary draws keep the existing pass. */
    assert(!defer_full_clear(&a,&depth,&rect,2,clear)); /* In-pass clear stays in-pass. */

    /* A clear through another color target sharing depth must affect the
     * next draw on the currently active color target too. */
    clear[1].clearValue.depthStencil = (VkClearDepthStencilValue){.875f,7};
    assert(defer_full_clear(&b,&depth,&rect,2,clear));
    begin_rendering(&a,&depth);
    assert(begins == 2 && ends == 1 && color_load == VK_ATTACHMENT_LOAD_OP_LOAD && depth_load == VK_ATTACHMENT_LOAD_OP_CLEAR);
    assert(depth_value.depth == .875f && depth_value.stencil == 7 && b.clear_pending);
    begin_rendering(&b,&depth);
    assert(begins == 3 && color_load == VK_ATTACHMENT_LOAD_OP_CLEAR && depth_load == VK_ATTACHMENT_LOAD_OP_LOAD);

    end_rendering();
    assert(defer_full_clear(&a,&depth,&rect,2,clear));
    unsigned before = begins;
    color_ready(&a); /* Sampling/copy/readback materializes color only. */
    assert(begins == before+1 && !s_in_render && !a.clear_pending && depth.clear_pending);
    assert(color_load == VK_ATTACHMENT_LOAD_OP_CLEAR && depth_load == VK_ATTACHMENT_LOAD_OP_DONT_CARE);
    color_ready(&a);
    assert(begins == before+1);
    color_ready(NULL);
    begin_rendering(&b,&depth);
    assert(depth_load == VK_ATTACHMENT_LOAD_OP_CLEAR && !depth.clear_pending);
    end_rendering();

    /* Reject partial rectangles/aspects without changing pending contents.
     * The caller then begins a pass (materializing the old full clear)
     * and executes its original partial-clear command. */
    assert(defer_full_clear(&a,NULL,&rect,1,clear));
    rect.rect.offset.x = 1;
    assert(!defer_full_clear(&a,NULL,&rect,1,clear));
    rect.rect.offset.x = 0;
    rect.rect.extent.width--;
    assert(!defer_full_clear(&a,NULL,&rect,1,clear));
    rect.rect.extent.width++;
    clear[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    assert(!defer_full_clear(&a,&depth,&rect,2,clear));
    assert(!defer_full_clear(&a,&depth,&rect,1,clear+1));
    clear[1].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    depth.pw++;
    assert(!defer_full_clear(&a,&depth,&rect,2,clear));
    depth.pw--;
    assert(!defer_full_clear(&a,NULL,&rect,2,clear));
    assert(!defer_full_clear(&a,NULL,&rect,0,clear));
    assert(a.clear_pending && !depth.clear_pending);
    begin_rendering(&a,&depth);
    assert(color_load == VK_ATTACHMENT_LOAD_OP_CLEAR && depth_load == VK_ATTACHMENT_LOAD_OP_LOAD);
    end_rendering();
    assert(defer_full_clear(&a,&depth,&rect,2,clear));
    VkSurf small = {.va=3, .pw=320, .ph=240};
    begin_rendering(&small,&depth);
    assert(depth_transfers == 1 && !depth.clear_pending);
    assert(depth_value.depth == .875f && depth_value.stencil == 7);
    assert(last_width == 320 && last_height == 240 && a.clear_pending);
    end_rendering();
    VkDepthBuf small_depth = {.pw=320, .ph=240};
    before = begins;
    begin_rendering(&a,&small_depth);
    assert(begins == before+2 && !a.clear_pending);
    assert(color_load == VK_ATTACHMENT_LOAD_OP_LOAD && last_width == 320 && last_height == 240);
    end_rendering();
    /* Coarse profiling must count passes without issuing any pass query into
     * its three-entry pool; this avoids pass timestamps in the overlap A/B. */
    s_gpu_profile = 2;
    frame.gpu_pass_count = 0;
    before = timestamps;
    unsigned passes_before = frame.gpu_passes;
    begin_rendering(&a,&depth);
    assert(timestamps == before && !frame.gpu_pass_count && frame.gpu_active_pass == -1);
    assert(frame.gpu_passes == passes_before + 1);
    end_rendering();
    /* The omitted image retains its contents for a later depth-using pass.
     * Exercise selection together with the actual pass/clear helpers. */
    uint32_t regs[0x400 / 4] = {0};
    unsigned depth_passes_before = frame.gpu_depth_passes;
    uint64_t depth_stores_before = frame.gpu_depth_store_pixels;
    assert(!draw_depth_attachment(&a,&depth,regs,1));
    begin_rendering(&a,draw_depth_attachment(&a,&depth,regs,1));
    assert(!s_rt_depth && frame.gpu_depth_passes == depth_passes_before);
    assert(frame.gpu_depth_store_pixels == depth_stores_before);
    regs[0x30C / 4] = 1;
    begin_rendering(&a,draw_depth_attachment(&a,&depth,regs,1));
    assert(s_rt_depth == &depth && frame.gpu_depth_passes == depth_passes_before + 1);
    assert(depth_load == VK_ATTACHMENT_LOAD_OP_LOAD);
    regs[0x30C / 4] = 0;
    assert(draw_depth_attachment(&a,&depth,regs,1) == &depth); /* No split. */
    before = begins;
    begin_rendering(&a,draw_depth_attachment(&a,&depth,regs,1));
    assert(begins == before);
    end_rendering();
    /* Refined mode keeps an offscreen pass intact and omits the recent
     * screen target's idle attachment, with no extra pass on following UI. */
    assert(draw_depth_attachment(&b,&depth,regs,2) == &depth);
    mark_screen_target(&a);
    before = begins;
    begin_rendering(&a,draw_depth_attachment(&a,&depth,regs,2));
    assert(!s_rt_depth && begins == before + 1);
    begin_rendering(&a,draw_depth_attachment(&a,&depth,regs,2));
    assert(begins == before + 1);
    end_rendering();
    b.clear_pending = 0;
    before = begins;
    begin_rendering(&b,draw_depth_attachment(&b,&depth,regs,2));
    assert(s_rt_depth == &depth);
    regs[0x30C / 4] = 1;
    begin_rendering(&b,draw_depth_attachment(&b,&depth,regs,2));
    assert(begins == before + 1); /* Offscreen prefix + depth use stay together. */
    end_rendering();
    puts("Deferred clears: full/partial eligibility, load ops, shared depth, overwrite, color-read materialization: PASS");
}
