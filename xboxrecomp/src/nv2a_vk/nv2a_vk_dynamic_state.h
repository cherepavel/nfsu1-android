/* Android's four core dynamic states. All guest pipelines declare these
 * dynamic. s_dyn_valid is cleared on a new command buffer and after binding
 * the presentation pipeline, which uses static blend/stencil state. */
typedef struct {
    uint32_t needed, emitted;
} VkDynamicStateWrites;

static uint32_t dynamic_state_write_count(uint32_t mask)
{
    return (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1) + ((mask >> 3) & 1);
}

static VkDynamicStateWrites android_apply_dynamic(const uint32_t *r, int cache)
{
    uint32_t color = r[0x34C / 4];
    uint32_t compare = r[0x36C / 4] & 255;
    uint32_t write = r[0x360 / 4] & 255;
    uint32_t reference = r[0x368 / 4] & 255;
    VkDynamicStateWrites result;
    result.needed = (!s_dyn_valid || color != s_dyn.bcolor ? 1u : 0u)
                  | (!s_dyn_valid || compare != s_dyn.smask ? 2u : 0u)
                  | (!s_dyn_valid || write != s_dyn.swrite ? 4u : 0u)
                  | (!s_dyn_valid || reference != s_dyn.sref ? 8u : 0u);
    result.emitted = cache ? result.needed : 15u;
    if (result.emitted & 1) {
        float bc[4];
        argb_vec4(color, bc);
        vkCmdSetBlendConstants(s_cb, bc);
    }
    if (result.emitted & 2)
        vkCmdSetStencilCompareMask(s_cb, VK_STENCIL_FACE_FRONT_AND_BACK, compare);
    if (result.emitted & 4)
        vkCmdSetStencilWriteMask(s_cb, VK_STENCIL_FACE_FRONT_AND_BACK, write);
    if (result.emitted & 8)
        vkCmdSetStencilReference(s_cb, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
    s_dyn.bcolor = color;
    s_dyn.smask = compare;
    s_dyn.swrite = write;
    s_dyn.sref = reference;
    s_dyn_valid = 1;
    return result;
}
