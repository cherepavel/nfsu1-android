/* Renderer-thread experiment: reuse a complete direct vertex upload within
 * one guest vertex-memory epoch and one upload-ring generation. Converted
 * attributes keep the original path because their scratch storage is reused.
 * Nv2aRawBatch documents the epoch/pointer lifetime contract shared with
 * instancing. No cached upload is accepted across an epoch change. */
#define VK_VERTEX_CACHE_SLOTS 64
#define VK_VERTEX_CACHE_MIN_BYTES 1024

typedef struct {
    uint32_t vertices, constants;
    uint16_t direct;
    struct {
        const uint8_t *ptr;
        uint32_t type, size, stride;
    } attr[NV2A_RAW_ATTRS];
} VkVertexUploadKey;

typedef struct {
    VkVertexUploadKey key;
    VkDeviceSize offsets[NV2A_RAW_ATTRS], end, bytes;
    uint32_t strides[NV2A_RAW_ATTRS];
    VkFormat formats[NV2A_RAW_ATTRS];
} VkVertexUploadEntry;

static struct {
    VkVertexUploadEntry entries[VK_VERTEX_CACHE_SLOTS];
    uint64_t valid;
    uint32_t epoch, ring_gen, bound_cb_gen;
    VkBuffer ring;
    int mode;
    VkVertexUploadEntry *bound;
} s_vertex_cache;

static struct {
    uint64_t draws, eligible, hits, copied_bytes, reusable_bytes, reused_bytes;
    uint64_t binds, skipped_binds;
} s_vertex_stats;

typedef struct {
    VkVertexUploadKey key;
    unsigned slot;
    int hit;
} VkVertexUploadLookup;

static void vertex_cache_sync(const Nv2aRawBatch *b, int mode)
{
    if (s_vertex_cache.ring_gen != s_ring_gen || s_vertex_cache.ring != s_f->ring
     || s_vertex_cache.epoch != b->vtx_epoch || s_vertex_cache.mode != mode) {
        s_vertex_cache.valid = 0;
        s_vertex_cache.bound = NULL;
        s_vertex_cache.ring_gen = s_ring_gen;
        s_vertex_cache.ring = s_f->ring;
        s_vertex_cache.epoch = b->vtx_epoch;
        s_vertex_cache.mode = mode;
    }
}

static int vertex_cache_lookup(const Nv2aRawBatch *b, uint16_t direct,
                               uint16_t converted, VkDeviceSize bytes,
                               int mode, VkVertexUploadLookup *lookup)
{
    if (!mode) return 0;
    s_vertex_stats.draws++;
    if (converted || !direct || !b->attr_const_gen || bytes < VK_VERTEX_CACHE_MIN_BYTES)
        return 0;
    vertex_cache_sync(b, mode);
    memset(lookup, 0, sizeof *lookup);
    lookup->key.vertices = b->vertex_count;
    lookup->key.constants = b->attr_const_gen;
    lookup->key.direct = direct;
    /* Only bucket selection uses the hash. Full keys decide equality. */
    uintptr_t hash = (uintptr_t)b->vertex_count * 2654435761u ^ direct;
    for (unsigned a = 0; a < NV2A_RAW_ATTRS; a++) {
        if (!(direct & (1u << a))) continue;
        lookup->key.attr[a].ptr = b->direct[a].ptr;
        lookup->key.attr[a].type = b->direct[a].type;
        lookup->key.attr[a].size = b->direct[a].size;
        lookup->key.attr[a].stride = b->direct[a].stride;
        hash ^= ((uintptr_t)b->direct[a].ptr >> 4) + (hash << 6) + (hash >> 2);
    }
    lookup->slot = (unsigned)(hash ^ (hash >> 16)) & (VK_VERTEX_CACHE_SLOTS - 1);
    VkVertexUploadEntry *entry = &s_vertex_cache.entries[lookup->slot];
    lookup->hit = (s_vertex_cache.valid & (UINT64_C(1) << lookup->slot))
        && entry->end <= s_f->ring_off
        && !memcmp(&entry->key, &lookup->key, sizeof entry->key);
    s_vertex_stats.eligible++;
    if (lookup->hit) {
        s_vertex_stats.hits++;
        s_vertex_stats.reusable_bytes += entry->bytes;
    }
    return 1;
}

static void vertex_cache_reuse(const VkVertexUploadLookup *lookup)
{
    VkVertexUploadEntry *entry = &s_vertex_cache.entries[lookup->slot];
    memcpy(android_vertex_strides, entry->strides, sizeof entry->strides);
    memcpy(android_vertex_formats, entry->formats, sizeof entry->formats);
    if (s_vertex_cache.bound == entry && s_vertex_cache.bound_cb_gen == s_cb_gen) {
        s_vertex_stats.skipped_binds++;
    } else {
        VkBuffer buffers[NV2A_RAW_ATTRS];
        for (unsigned a = 0; a < NV2A_RAW_ATTRS; a++) buffers[a] = s_f->ring;
        vkCmdBindVertexBuffers(s_cb, 0, NV2A_RAW_ATTRS, buffers, entry->offsets);
        s_vertex_stats.binds++;
        s_vertex_cache.bound = entry;
        s_vertex_cache.bound_cb_gen = s_cb_gen;
    }
    s_vertex_stats.reused_bytes += entry->bytes;
}

static void vertex_cache_record(const Nv2aRawBatch *b, int mode, int eligible,
                                const VkVertexUploadLookup *lookup,
                                const VkDeviceSize *offsets, VkDeviceSize bytes)
{
    if (!mode) return;
    s_vertex_stats.copied_bytes += bytes;
    s_vertex_stats.binds++;
    /* Any ordinary upload replaces command-buffer vertex binding state. */
    s_vertex_cache.bound = NULL;
    if (!eligible) return;
    /* ring_alloc may have flushed/recycled the ring since lookup. */
    vertex_cache_sync(b, mode);
    VkVertexUploadEntry *entry = &s_vertex_cache.entries[lookup->slot];
    entry->key = lookup->key;
    memcpy(entry->offsets, offsets, sizeof entry->offsets);
    memcpy(entry->strides, android_vertex_strides, sizeof entry->strides);
    memcpy(entry->formats, android_vertex_formats, sizeof entry->formats);
    entry->end = s_f->ring_off;
    entry->bytes = bytes;
    s_vertex_cache.valid |= UINT64_C(1) << lookup->slot;
    s_vertex_cache.bound = entry;
    s_vertex_cache.bound_cb_gen = s_cb_gen;
}
