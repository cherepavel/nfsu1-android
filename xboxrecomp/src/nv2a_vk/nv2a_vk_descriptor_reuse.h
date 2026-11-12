/* Frame-owned descriptor allocations. The cursor resets only after this
 * Frame's fence completes, at the same point as the legacy pool reset.
 * Every returned set is updated before binding, and is used at most once
 * before the next reset. No descriptor-content cache or dynamic offsets. */
#define ANDROID_REUSE_SETS_PER_POOL 8192u
#define ANDROID_REUSE_MAX_POOLS 2u

typedef struct {
    VkDescriptorSet *sets;
    VkDescriptorSetLayout layout;
    uint32_t count, capacity, used;
} AndroidDescriptorList;

typedef struct AndroidDescriptorReuse {
    VkDescriptorPool pools[ANDROID_REUSE_MAX_POOLS];
    uint32_t pool_count, pool_sets, used;
    AndroidDescriptorList lists[2]; /* guest and presentation layouts */
} AndroidDescriptorReuse;

static struct {
    uint64_t requests, reused, allocated, pools;
} s_android_descriptor_reuse_stats;

static VkResult android_descriptor_reuse_get(Frame *frame, VkDescriptorSetLayout layout,
    int present, VkDescriptorSet *out)
{
    *out = VK_NULL_HANDLE;
    if (!frame->descriptor_reuse) {
        frame->descriptor_reuse = calloc(1, sizeof *frame->descriptor_reuse);
        if (!frame->descriptor_reuse) return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    AndroidDescriptorReuse *reuse = frame->descriptor_reuse;
    /* Preserve the previous per-recording limit, including presentation. */
    if (reuse->used >= ANDROID_REUSE_SETS_PER_POOL)
        return VK_ERROR_OUT_OF_POOL_MEMORY;
    AndroidDescriptorList *list = &reuse->lists[present != 0];
    if (list->layout && list->layout != layout) return VK_ERROR_INITIALIZATION_FAILED;
    if (list->used < list->count) {
        *out = list->sets[list->used++];
        reuse->used++;
        s_android_descriptor_reuse_stats.requests++;
        s_android_descriptor_reuse_stats.reused++;
        return VK_SUCCESS;
    }
    if (list->count == list->capacity) {
        uint32_t capacity = list->capacity ? list->capacity * 2 : 64;
        if (capacity > ANDROID_REUSE_SETS_PER_POOL) capacity = ANDROID_REUSE_SETS_PER_POOL;
        VkDescriptorSet *sets = realloc(list->sets, (size_t)capacity * sizeof *sets);
        if (!sets) return VK_ERROR_OUT_OF_HOST_MEMORY;
        list->sets = sets;
        list->capacity = capacity;
    }
    if (!reuse->pool_count || reuse->pool_sets == ANDROID_REUSE_SETS_PER_POOL) {
        if (reuse->pool_count == ANDROID_REUSE_MAX_POOLS)
            return VK_ERROR_OUT_OF_POOL_MEMORY;
        VkDescriptorPoolSize sizes[2] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 * ANDROID_REUSE_SETS_PER_POOL},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * ANDROID_REUSE_SETS_PER_POOL}
        };
        VkDescriptorPoolCreateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        info.maxSets = ANDROID_REUSE_SETS_PER_POOL;
        info.poolSizeCount = 2;
        info.pPoolSizes = sizes;
        VkDescriptorPool pool;
        VkResult result = vkCreateDescriptorPool(s_dev, &info, NULL, &pool);
        if (result != VK_SUCCESS) return result;
        reuse->pools[reuse->pool_count++] = pool;
        reuse->pool_sets = 0;
        s_android_descriptor_reuse_stats.pools++;
    }
    VkDescriptorSetAllocateInfo allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = reuse->pools[reuse->pool_count - 1];
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &layout;
    VkDescriptorSet set;
    VkResult result = vkAllocateDescriptorSets(s_dev, &allocate, &set);
    if (result != VK_SUCCESS) return result;
    list->layout = layout;
    list->sets[list->count++] = set;
    list->used++;
    reuse->pool_sets++;
    reuse->used++;
    *out = set;
    s_android_descriptor_reuse_stats.requests++;
    s_android_descriptor_reuse_stats.allocated++;
    return VK_SUCCESS;
}

static void android_descriptor_reuse_reset(Frame *frame)
{
    if (!frame->descriptor_reuse) return;
    AndroidDescriptorReuse *reuse = frame->descriptor_reuse;
    reuse->used = 0;
    reuse->lists[0].used = reuse->lists[1].used = 0;
    static uint32_t reported;
    if (s_android_descriptor_reuse && s_frame - reported >= 300) {
        NFS_LOG_INFO("performance", "[VKSET] frame=%u requests=%llu reused=%llu allocated=%llu new_pools=%llu retained_guest=%u retained_present=%u frame_pools=%u (since report; retained for this frame slot)\n",
            s_frame, (unsigned long long)s_android_descriptor_reuse_stats.requests,
            (unsigned long long)s_android_descriptor_reuse_stats.reused,
            (unsigned long long)s_android_descriptor_reuse_stats.allocated,
            (unsigned long long)s_android_descriptor_reuse_stats.pools,
            reuse->lists[0].count, reuse->lists[1].count, reuse->pool_count);
        memset(&s_android_descriptor_reuse_stats, 0, sizeof s_android_descriptor_reuse_stats);
        reported = s_frame;
    }
}
