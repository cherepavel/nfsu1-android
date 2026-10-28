/* Opt-in output-sized UI on the renderer thread, with an ordered game marker.
 * Each guest surface retains its high-resolution image in the alternate slot.
 * Only color-only UI draws may switch; depth/stencil storage is never resized. */
typedef struct {
    VkImage image;
    VkImageView view;
    VkDeviceMemory mem;
    uint32_t pw, ph;
    int active;
} VkNativeUI;
static VkNativeUI s_native_ui[VK_MAX_SURF];
static int s_native_ui_phase;
static uint64_t s_native_ui_switches, s_native_ui_restores, s_native_ui_preserves;
static uint64_t s_native_ui_draws, s_native_ui_begins, s_native_ui_ends;

static int native_ui_enabled(void)
{
#ifdef __ANDROID__
    static int enabled = -1;
    if (enabled < 0) {
        char property[PROP_VALUE_MAX] = {0};
        const char *value = getenv("RECOMP_VK_UI_NATIVE");
        if (!value && __system_property_get("debug.nfsu.ui_native", property) > 0) value = property;
        enabled = value && !strcmp(value, "1");
        NFS_LOG_INFO("graphics", "[VKUI] native=%d (startup)\n", enabled);
    }
    return enabled;
#else
    return 0;
#endif
}

/* Same logical letterbox as final presentation, independent of rotation. */
static VkRect2D native_ui_output_rect(VkSurf *s)
{
    VkRect2D rect = {0};
    if (!s || !s->w || !s->h || !s_sc_logical_extent.width || !s_sc_logical_extent.height)
        return rect;
    float lw = (float)s->w / (float)s->aa_sx, lh = (float)s->h / (float)s->aa_sy;
    float ww = (float)s_sc_logical_extent.width, wh = (float)s_sc_logical_extent.height;
    if (xbox_video_widescreen() && lw < lh * 1.5f) lw = lh * 16.0f / 9.0f;
    float k = ww / lw < wh / lh ? ww / lw : wh / lh;
    rect.extent.width = (uint32_t)(lw * k);
    rect.extent.height = (uint32_t)(lh * k);
    rect.offset.x = ((int32_t)s_sc_logical_extent.width - (int32_t)rect.extent.width) / 2;
    rect.offset.y = ((int32_t)s_sc_logical_extent.height - (int32_t)rect.extent.height) / 2;
    return rect;
}

static void native_ui_swap(VkSurf *s, VkNativeUI *other)
{
    VkImage image = s->image; VkImageView view = s->view; VkDeviceMemory mem = s->mem;
    uint32_t pw = s->pw, ph = s->ph;
    s->image = other->image; s->view = other->view; s->mem = other->mem;
    s->pw = other->pw; s->ph = other->ph;
    other->image = image; other->view = view; other->mem = mem;
    other->pw = pw; other->ph = ph;
    s->gen++;
    s_rt = NULL; s_rt_depth = NULL;
}

static void native_ui_blit(VkSurf *s, VkNativeUI *dst)
{
    VkImageBlit bl = {0};
    color_ready(s);
    end_rendering();
    barrier_all();
    bl.srcSubresource.aspectMask = bl.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bl.srcSubresource.layerCount = bl.dstSubresource.layerCount = 1;
    bl.srcOffsets[1].x = (int32_t)s->pw; bl.srcOffsets[1].y = (int32_t)s->ph; bl.srcOffsets[1].z = 1;
    bl.dstOffsets[1].x = (int32_t)dst->pw; bl.dstOffsets[1].y = (int32_t)dst->ph; bl.dstOffsets[1].z = 1;
    if (s_f->gpu_queries) s_f->gpu_blits++;
    vkCmdBlitImage(s_cb, s->image, VK_IMAGE_LAYOUT_GENERAL, dst->image,
                  VK_IMAGE_LAYOUT_GENERAL, 1, &bl, VK_FILTER_LINEAR);
    barrier_all();
}

static void native_ui_restore(VkSurf *s, int discard)
{
    VkNativeUI *other = &s_native_ui[s - s_surf];
    if (!other->active) return;
    if (!discard) {
        native_ui_blit(s, other);
        s_native_ui_preserves++;
    } else {
        end_rendering();
    }
    native_ui_swap(s, other);
    other->active = 0;
    s_native_ui_restores++;
}

static void native_ui_drop(VkSurf *s)
{
    VkNativeUI *other = &s_native_ui[s - s_surf];
    if (other->image) garbage_add(other->image, other->view, other->mem);
    memset(other, 0, sizeof *other);
}

static int native_ui_full_clear(VkSurf *s, uint32_t flags)
{
    if ((flags & 0xF0) != 0xF0 || !s_regs) return 0;
    uint32_t hz = s_regs[0x1D98 / 4], vt = s_regs[0x1D9C / 4];
    return !(hz & 0xFFFF) && !(vt & 0xFFFF)
        && ((hz >> 16) + 1) * s->aa_sx >= s->w
        && ((vt >> 16) + 1) * s->aa_sy >= s->h;
}

static int native_ui_color_only(const uint32_t *r)
{
    return s_native_ui_phase && r && !r[0x30C/4] && !r[0x35C/4] && !r[0x32C/4];
}

static void native_ui_select(VkSurf *s, int allowed, uint32_t clear_flags)
{
    VkNativeUI *other = &s_native_ui[s - s_surf];
    if (!allowed || !native_ui_enabled() || !s->screen_seen || s_frame - s->screen_frame > 4) {
        native_ui_restore(s, native_ui_full_clear(s, clear_flags));
        return;
    }
    VkRect2D output = native_ui_output_rect(s);
    uint32_t pw = output.extent.width, ph = output.extent.height;
    if (other->active && s->pw == pw && s->ph == ph) return;
    native_ui_restore(s, 0);
    /* This experiment never increases the scale or changes an offscreen RT. */
    if (!pw || !ph || pw > s->pw || ph > s->ph || (pw == s->pw && ph == s->ph)) return;
    if (other->image && (other->pw != pw || other->ph != ph)) native_ui_drop(s);
    if (!other->image) {
        if (!make_image(pw, ph, VK_FORMAT_B8G8R8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, &other->image, &other->view, &other->mem))
            return;
        other->pw = pw; other->ph = ph;
    }
    native_ui_blit(s, other);
    native_ui_swap(s, other);
    other->active = 1;
    if (!s_native_ui_switches)
        NFS_LOG_INFO("graphics", "[VKUI] target=%08X scene=%ux%u ui=%ux%u\n",
            s->va, other->pw, other->ph, s->pw, s->ph);
    s_native_ui_switches++;
}

static void native_ui_phase(int begin)
{
    /* A held instanced draw belongs to the preceding scope. Do not let
     * append/flush cross the resolution boundary. */
    pend_flush();
    if (begin) s_native_ui_begins++; else s_native_ui_ends++;
    s_native_ui_phase = native_ui_enabled() && begin;
}
