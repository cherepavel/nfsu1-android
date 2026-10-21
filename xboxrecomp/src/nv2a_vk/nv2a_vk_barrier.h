/* Same-queue dependencies, outside rendering. Kept separate for the mock
 * driver test. General barriers still cover transfers, readback and present. */
static void barrier_all(void)
{
    if (s_f->gpu_queries) s_f->gpu_barriers++;
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(s_cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    for (int i = 0; i < VK_MAX_SURF; i++)
        s_surf[i].dirty = 0;
}

static int scoped_pass_barrier_enabled(void)
{
#ifdef __ANDROID__
    static int enabled = -1;
    static uint32_t checked;
    if (enabled < 0 || s_frame - checked >= 300) {
        char property[PROP_VALUE_MAX] = {0};
        __system_property_get("debug.nfsu.pass_barrier", property);
        int next = !strcmp(property, "1");
        if (next != enabled)
            NFS_LOG_INFO("performance", "[VKBARRIER] scoped_pass=%d frame=%u\n", next, s_frame);
        enabled = next;
        checked = s_frame;
    }
    return enabled;
#else
    return 0;
#endif
}

static void barrier_before_rendering(void)
{
    if (!scoped_pass_barrier_enabled()) {
        barrier_all();
        return;
    }
    /* Keep the original producer scope, including transfers and preceding
     * command buffers/submissions. Only narrow the consumers of GPU writes:
     * fragment texture reads and color/depth/stencil attachment accesses.
     * Both early AND late tests are required for shared depth/stencil.
     * No BY_REGION: sampled pixels need not match framebuffer coordinates.
     *
     * Vertex/index/uniform data comes from the host-written frame ring,
     * protected by frame fences. The vertex shaders neither sample images
     * nor consume GPU-written buffers. Thus the next pass's vertex work can
     * start without waiting for preceding fragment work. Revisit this scope
     * if GPU-produced geometry/uniforms or vertex texture fetches are added.
     * WAR ordering is retained by the ALL_COMMANDS source execution scope. */
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
        | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(s_cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
        | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 1, &mb, 0, NULL, 0, NULL);
    if (s_f->gpu_queries) s_f->gpu_pass_barriers++;
    for (int i = 0; i < VK_MAX_SURF; i++)
        s_surf[i].dirty = 0;
}
