/* Attachment-content bookkeeping shared with the mock-driver regression. */
static void begin_rendering(VkSurf *s, VkDepthBuf *d)
{
    VkRenderingAttachmentInfo ca = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    VkRenderingAttachmentInfo da = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO };

    /* target() normally pairs equal stored sizes. Preserve the full pending
     * clear even if a caller attaches a smaller image on either side. */
    if (d && s->clear_pending && (d->pw < s->pw || d->ph < s->ph)) {
        begin_rendering(s, NULL);
        end_rendering();
    }
    if (d && d->clear_pending && (s->pw < d->pw || s->ph < d->ph)) {
        VkImageSubresourceRange range = {
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1
        };
        end_rendering();
        barrier_all();
        vkCmdClearDepthStencilImage(s_cb, d->image, VK_IMAGE_LAYOUT_GENERAL,
            &d->clear_value, 1, &range);
        d->clear_pending = 0;
        barrier_all();
    }
    if (s_in_render && s_rt == s && s_rt_depth == d &&
        !s->clear_pending && !(d && d->clear_pending))
        return;
    end_rendering();
    barrier_all();
    ca.imageView = s->view;
    ca.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    ca.loadOp = s->clear_pending ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.clearValue.color = s->clear_value;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ri.renderArea.extent.width = s->pw;
    ri.renderArea.extent.height = s->ph;
    if (d) {
        if (d->pw < ri.renderArea.extent.width) ri.renderArea.extent.width = d->pw;
        if (d->ph < ri.renderArea.extent.height) ri.renderArea.extent.height = d->ph;
    }
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    if (d) {
        da.imageView = d->view;
        da.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        da.loadOp = d->clear_pending ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        da.clearValue.depthStencil = d->clear_value;
        da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ri.pDepthAttachment = &da;
        ri.pStencilAttachment = &da;
    }
    if (s_f->gpu_queries) {
        s_f->gpu_passes++;
        if (d) {
            uint64_t pixels = (uint64_t)ri.renderArea.extent.width * ri.renderArea.extent.height;
            s_f->gpu_depth_passes++;
            if (!d->clear_pending) s_f->gpu_depth_load_pixels += pixels;
            s_f->gpu_depth_store_pixels += pixels;
        }
        if (s_gpu_profile == 1 && s_f->gpu_pass_count < GPU_PASS_LIMIT) {
            uint32_t slot = s_f->gpu_pass_count++;
            s_f->gpu_targets[slot].draws = s_f->gpu_targets[slot].indices = 0;
            s_f->gpu_targets[slot].compact_eligible = s_f->gpu_targets[slot].compact_selected = 0;
            s_f->gpu_targets[slot].clears = s->clear_pending || (d && d->clear_pending);
            s_f->gpu_targets[slot].depth = d != NULL;
            s_f->gpu_targets[slot].end_reason = NULL;
            s_f->gpu_targets[slot].target = s->va;
            s_f->gpu_targets[slot].width = ri.renderArea.extent.width;
            s_f->gpu_targets[slot].height = ri.renderArea.extent.height;
            s_f->gpu_active_pass = (int)slot;
            vkCmdWriteTimestamp(s_cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                s_f->gpu_queries, 3 + slot * 2);
        }
    }
    p_begin_rendering(s_cb, &ri);
    s->clear_pending = 0;
    if (d) d->clear_pending = 0;
    s_in_render = 1;
    s_rt = s;
    s_rt_depth = d;
    set_viewport(s->pw, s->ph);
}


/* A pending color clear must become visible before sampling, copying,
 * readback or presentation, even when no draw ever used this target. */
static void color_ready(VkSurf *s)
{
    if (!s || !s->clear_pending) return;
    begin_rendering(s, NULL);
    end_rendering();
    barrier_all();
}

/* Only complete color clears, optionally with BOTH depth/stencil aspects.
 * Partial rectangles/aspects retain vkCmdClearAttachments. A pending depth
 * clear is shared by all color targets using that same depth image. */
static int defer_full_clear(VkSurf *s, VkDepthBuf *d,
    const VkClearRect *rect, uint32_t count, const VkClearAttachment *attachments)
{
    if (s_in_render && s_rt == s && s_rt_depth == d) return 0;
    if (!count || count > 2 || attachments[0].aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        rect->rect.offset.x || rect->rect.offset.y || rect->baseArrayLayer || rect->layerCount != 1 ||
        rect->rect.extent.width != s->pw || rect->rect.extent.height != s->ph) return 0;
    if (count == 2 && (!d || d->pw != s->pw || d->ph != s->ph ||
        attachments[1].aspectMask != (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))) return 0;
    s->clear_pending = 1;
    s->clear_value = attachments[0].clearValue.color;
    if (count == 2) {
        d->clear_pending = 1;
        d->clear_value = attachments[1].clearValue.depthStencil;
    }
    return 1;
}

static int lazy_clears_enabled(void)
{
#ifdef __ANDROID__
    static int enabled = -1;
    static uint32_t checked;
    if (enabled < 0 || s_frame - checked >= 300) {
        char property[PROP_VALUE_MAX] = {0};
        __system_property_get("debug.nfsu.lazy_clear", property);
        int next = !strcmp(property, "1");
        if (next != enabled)
            NFS_LOG_INFO("graphics", "[VKCLR] deferred full clears=%d frame=%u\n", next, s_frame);
        enabled = next;
        checked = s_frame;
    }
    return enabled;
#else
    return 0;
#endif
}
