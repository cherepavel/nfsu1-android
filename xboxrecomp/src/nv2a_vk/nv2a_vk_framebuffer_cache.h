/* Renderer-thread cache. Evicted entries follow the recording frame's fence,
 * just like the image views they reference. No attachment contents are cached. */
#define ANDROID_FRAMEBUFFER_CACHE_SIZE 128
static struct {
    VkRenderPass pass;
    VkImageView views[2];
    uint32_t count, width, height;
    uint64_t used;
    VkFramebuffer framebuffer;
} s_android_framebuffers[ANDROID_FRAMEBUFFER_CACHE_SIZE];
static uint64_t s_android_framebuffer_serial;
static unsigned s_android_framebuffer_hits, s_android_framebuffer_creates;

static void android_defer_framebuffer(Frame *frame, VkFramebuffer framebuffer)
{
    if (frame->nframebuffers == frame->framebuffer_cap) {
        uint32_t cap = frame->framebuffer_cap ? frame->framebuffer_cap * 2 : 64;
        VkFramebuffer *next = realloc(frame->framebuffers, cap * sizeof(*next));
        if (!next) abort();
        frame->framebuffers = next;
        frame->framebuffer_cap = cap;
    }
    frame->framebuffers[frame->nframebuffers++] = framebuffer;
}

/* At guest-image retirement, defer the framebuffer alongside the view.
 * Swapchain views use NULL only after presentation has waited for device idle. */
static void android_framebuffer_retire_view(VkImageView view, Frame *frame)
{
    if (!view) return;
    for (unsigned i = 0; i < ANDROID_FRAMEBUFFER_CACHE_SIZE; i++) {
        if (!s_android_framebuffers[i].framebuffer ||
            (s_android_framebuffers[i].views[0] != view &&
             s_android_framebuffers[i].views[1] != view)) continue;
        if (frame) android_defer_framebuffer(frame, s_android_framebuffers[i].framebuffer);
        else vkDestroyFramebuffer(s_dev, s_android_framebuffers[i].framebuffer, NULL);
        memset(&s_android_framebuffers[i], 0, sizeof(s_android_framebuffers[i]));
    }
}

static VkFramebuffer android_framebuffer_get(const VkFramebufferCreateInfo *create, int enabled)
{
    unsigned slot = 0;
    if (enabled) {
        for (unsigned i = 0; i < ANDROID_FRAMEBUFFER_CACHE_SIZE; i++) {
            if (s_android_framebuffers[i].framebuffer &&
                s_android_framebuffers[i].pass == create->renderPass &&
                s_android_framebuffers[i].count == create->attachmentCount &&
                s_android_framebuffers[i].width == create->width &&
                s_android_framebuffers[i].height == create->height &&
                s_android_framebuffers[i].views[0] == create->pAttachments[0] &&
                s_android_framebuffers[i].views[1] ==
                    (create->attachmentCount == 2 ? create->pAttachments[1] : VK_NULL_HANDLE)) {
                s_android_framebuffers[i].used = ++s_android_framebuffer_serial;
                s_android_framebuffer_hits++;
                return s_android_framebuffers[i].framebuffer;
            }
            if (s_android_framebuffers[i].used < s_android_framebuffers[slot].used) slot = i;
        }
    }
    VkFramebuffer framebuffer;
    if (vkCreateFramebuffer(s_dev, create, NULL, &framebuffer) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    s_android_framebuffer_creates++;
    if (!enabled) {
        android_defer_framebuffer(s_f, framebuffer);
        return framebuffer;
    }
    if (s_android_framebuffers[slot].framebuffer)
        android_defer_framebuffer(s_f, s_android_framebuffers[slot].framebuffer);
    s_android_framebuffers[slot].pass = create->renderPass;
    s_android_framebuffers[slot].count = create->attachmentCount;
    s_android_framebuffers[slot].width = create->width;
    s_android_framebuffers[slot].height = create->height;
    s_android_framebuffers[slot].views[0] = create->pAttachments[0];
    s_android_framebuffers[slot].views[1] = create->attachmentCount == 2
        ? create->pAttachments[1] : VK_NULL_HANDLE;
    s_android_framebuffers[slot].used = ++s_android_framebuffer_serial;
    s_android_framebuffers[slot].framebuffer = framebuffer;
    return framebuffer;
}
