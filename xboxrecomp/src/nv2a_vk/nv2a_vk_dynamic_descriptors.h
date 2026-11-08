/* Startup-only experiment: reuse immutable texture/range descriptor sets
 * within one Frame; supply the three ring offsets at bind time. The frame
 * fence completes before its pool and this index are reset together. */
#define ANDROID_DESCRIPTOR_CACHE_ENTRIES 4096u
#define ANDROID_DESCRIPTOR_CACHE_SLOTS (ANDROID_DESCRIPTOR_CACHE_ENTRIES * 2u)

typedef struct {
    VkDescriptorBufferInfo buffers[3]; /* offset is always zero */
    VkDescriptorImageInfo images[4];
} AndroidDescriptorKey;

typedef struct AndroidDescriptorCache {
    uint32_t slots[ANDROID_DESCRIPTOR_CACHE_SLOTS]; /* entry index + 1 */
    uint32_t count;
    struct { AndroidDescriptorKey key; VkDescriptorSet set; } entries[ANDROID_DESCRIPTOR_CACHE_ENTRIES];
} AndroidDescriptorCache;

/* Canonical bindings 0,1,6 -> dynamic offsets 0,1,2. Ranges are retained:
 * binding 6 can shrink near the end of the ring. No VK_WHOLE_SIZE is used. */
static int android_descriptor_key(const VkWriteDescriptorSet *writes,
    VkDeviceSize alignment, AndroidDescriptorKey *key, uint32_t offsets[3])
{
    uint32_t mask = 0;
    memset(key, 0, sizeof *key);
    for (uint32_t i = 0; i < 7; i++) {
        const VkWriteDescriptorSet *w = &writes[i];
        uint32_t binding = w->dstBinding;
        if (binding >= 7 || (mask & (1u << binding)) || w->descriptorCount != 1 || w->dstArrayElement)
            return 0;
        mask |= 1u << binding;
        if (binding < 2 || binding == 6) {
            if (w->descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || !w->pBufferInfo)
                return 0;
            uint32_t at = binding == 6 ? 2 : binding;
            const VkDescriptorBufferInfo *b = w->pBufferInfo;
            if (!alignment || b->offset % alignment || b->offset > UINT32_MAX
                || !b->range || b->range == VK_WHOLE_SIZE
                || b->offset > RING_BYTES || b->range > RING_BYTES - b->offset)
                return 0;
            offsets[at] = (uint32_t)b->offset;
            key->buffers[at].buffer = b->buffer;
            key->buffers[at].range = b->range;
        } else {
            if (w->descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || !w->pImageInfo)
                return 0;
            VkDescriptorImageInfo *image = &key->images[binding - 2];
            image->sampler = w->pImageInfo->sampler;
            image->imageView = w->pImageInfo->imageView;
            image->imageLayout = w->pImageInfo->imageLayout;
        }
    }
    return mask == 0x7f;
}

static uint32_t android_descriptor_slot(const AndroidDescriptorCache *cache,
    const AndroidDescriptorKey *key)
{
    _Static_assert(sizeof *key % sizeof(uint32_t) == 0, "descriptor key word size");
    const unsigned char *bytes = (const unsigned char *)key;
    uint32_t hash = 2166136261u;
    /* Keys are canonical, including zeroed padding. Hash words rather than
     * a bytewise dependency chain; memcpy avoids alignment/aliasing assumptions.
     * Fold high bits before masking: handles/ranges often have aligned low bits.
     * Full key comparison below remains authoritative on hash collisions. */
    for (size_t i = 0; i < sizeof *key; i += sizeof(uint32_t)) {
        uint32_t word;
        memcpy(&word, bytes + i, sizeof word);
        hash = (hash ^ word) * 16777619u;
    }
    hash ^= hash >> 16;
    hash *= 0x85ebca6bu;
    hash ^= hash >> 13;
    uint32_t slot = hash & (ANDROID_DESCRIPTOR_CACHE_SLOTS - 1u);
    while (cache->slots[slot]
        && memcmp(&cache->entries[cache->slots[slot] - 1].key, key, sizeof *key))
        slot = (slot + 1u) & (ANDROID_DESCRIPTOR_CACHE_SLOTS - 1u);
    return slot;
}

static uint64_t s_android_descriptor_hits, s_android_descriptor_misses;
static void android_descriptor_cache_reset(Frame *frame)
{
    if (frame->descriptor_cache) {
        memset(frame->descriptor_cache->slots, 0, sizeof frame->descriptor_cache->slots);
        frame->descriptor_cache->count = 0;
    }
    /* One report per 300 flips; no property queries or clocks per draw. */
    static uint32_t reported;
    if (s_android_dynamic_descriptors && s_frame - reported >= 300) {
        NFS_LOG_INFO("performance", "[VKDYN] frame=%u hits=%llu misses=%llu (since report)\n",
            s_frame, (unsigned long long)s_android_descriptor_hits,
            (unsigned long long)s_android_descriptor_misses);
        s_android_descriptor_hits = s_android_descriptor_misses = 0;
        reported = s_frame;
    }
}

static void VKAPI_PTR android_dynamic_descriptors(VkCommandBuffer command, VkPipelineBindPoint point,
    VkPipelineLayout layout, uint32_t index, uint32_t count, const VkWriteDescriptorSet *writes)
{
    if (count != 7) { /* FXAA's one sampler keeps the existing path. */
        android_push_descriptors(command, point, layout, index, count, writes);
        return;
    }
    AndroidDescriptorKey key;
    uint32_t offsets[3];
    if (!android_descriptor_key(writes, s_ubo_align, &key, offsets)) {
        NFS_LOG_FATAL("graphics", "[VKDYN] unexpected descriptor/ring shape\n"); abort();
    }
    if (!s_f->descriptor_cache) {
        s_f->descriptor_cache = calloc(1, sizeof *s_f->descriptor_cache);
        if (!s_f->descriptor_cache) abort();
    }
    AndroidDescriptorCache *cache = s_f->descriptor_cache;
    uint32_t slot = android_descriptor_slot(cache, &key);
    VkDescriptorSet set;
    if (cache->slots[slot]) {
        set = cache->entries[cache->slots[slot] - 1].set;
        s_android_descriptor_hits++;
    } else {
        android_descriptor_pool_ready();
        VkDescriptorSetAllocateInfo allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = s_f->descriptors;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &s_dsl;
        if (vkAllocateDescriptorSets(s_dev, &allocate, &set) != VK_SUCCESS) {
            NFS_LOG_FATAL("graphics", "[VKDYN] descriptor pool exhausted\n"); abort();
        }
        VkWriteDescriptorSet updated[7] = {0};
        for (uint32_t binding = 0; binding < 7; binding++) {
            VkWriteDescriptorSet *w = &updated[binding];
            w->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w->dstSet = set;
            w->dstBinding = binding;
            w->descriptorCount = 1;
            if (binding < 2 || binding == 6) {
                w->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                w->pBufferInfo = &key.buffers[binding == 6 ? 2 : binding];
            } else {
                w->descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w->pImageInfo = &key.images[binding - 2];
            }
        }
        vkUpdateDescriptorSets(s_dev, 7, updated, 0, NULL);
        /* Full CPU index: still allocate a correct uncached set. Never evict
         * or modify a set already referenced by a recorded/in-flight draw. */
        if (cache->count < ANDROID_DESCRIPTOR_CACHE_ENTRIES) {
            uint32_t at = cache->count++;
            cache->entries[at].key = key;
            cache->entries[at].set = set;
            cache->slots[slot] = at + 1;
        }
        s_android_descriptor_misses++;
    }
    vkCmdBindDescriptorSets(command, point, layout, index, 1, &set, 3, offsets);
}
