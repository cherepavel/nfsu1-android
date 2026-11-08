/* Draw-only attachment selection. Clear commands always use the original
 * depth image. Never discard its contents: a later draw can attach it again. */
static void mark_screen_target(VkSurf *s)
{
    if (!s) return;
    s->screen_seen = 1;
    s->screen_frame = s_frame;
}

static VkDepthBuf *draw_depth_attachment(VkSurf *s, VkDepthBuf *d,
                                        const uint32_t *r, int mode)
{
    if ((mode != 1 && mode != 2) || !d || !r || r[0x30C / 4] || r[0x35C / 4] || r[0x32C / 4]
        || d->clear_pending || d->pw != s->pw || d->ph != s->ph)
        return d;
    /* Mode 1 split the offscreen light pass into fade + depth-using lights
     * on Adreno. Mode 2 confines the opportunity to recent flip sources;
     * this retains the observed color-only UI pass without that split.
     * This is a performance guard, not proof about future draws. It handles
     * alternating guest buffers without depending on a guest address. */
    if (mode == 2 && (!s->screen_seen || s_frame - s->screen_frame > 4u))
        return d;
    /* Keep an already-open compatible pass, even though this draw does not
     * use depth. Otherwise this optimization would introduce a store/reload.
     * A pending color clear already forces a new pass, so it can omit depth. */
    if (s_in_render && s_rt == s && s_rt_depth == d && !s->clear_pending)
        return d;
    return NULL;
}

static int unused_depth_enabled(void)
{
#ifdef __ANDROID__
    static int enabled = -1;
    static uint32_t checked;
    if (enabled < 0 || s_frame - checked >= 300) {
        char property[PROP_VALUE_MAX] = {0};
        __system_property_get("debug.nfsu.unused_depth", property);
        int next = !strcmp(property, "1") ? 1 : !strcmp(property, "2") ? 2 : 0;
        if (next != enabled)
            NFS_LOG_INFO("performance", "[VKDEPTH] omit_unused=%d frame=%u screen_only=%d\n", next, s_frame, next == 2);
        enabled = next;
        checked = s_frame;
    }
    return enabled;
#else
    return 0;
#endif
}
