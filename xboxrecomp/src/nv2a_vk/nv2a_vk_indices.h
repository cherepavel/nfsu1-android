/* Plan without allocating or reading indices. Packing consumes the borrowed
 * source synchronously and writes final Vulkan indices directly to the ring. */
#include "../kernel/nv2a_indices.h"

typedef struct {
    uint32_t count, primitive_class;
    VkPrimitiveTopology topology;
    VkIndexType type;
    VkDeviceSize bytes;
} VkIndexUpload;

static int index_upload_plan(const Nv2aRawBatch *b, VkIndexUpload *out)
{
    uint64_t count = b->index_count;
    memset(out, 0, sizeof *out);
    switch (b->prim) {
    case 1: out->topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; out->primitive_class = 0; break;
    case 2: out->topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; out->primitive_class = 1; break;
    case 3:
        if (!count) return 0;
        count++;
        out->topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; out->primitive_class = 1; break;
    case 4: out->topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; out->primitive_class = 1; break;
    case 5: out->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; out->primitive_class = 2; break;
    case 6: case 9:
        out->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; out->primitive_class = 2; break;
    case 7: case 10:
        out->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN; out->primitive_class = 2; break;
    case 8:
        count = count / 4 * 6;
        out->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; out->primitive_class = 2; break;
    default: return 0;
    }
    if (count > UINT32_MAX) return 0;
    out->count = (uint32_t)count;
    out->type = b->vertex_count <= 65536 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    out->bytes = count * (out->type == VK_INDEX_TYPE_UINT16 ? 2 : 4);
    return 1;
}

static void index_upload_fill(const Nv2aRawBatch *b, const VkIndexUpload *plan, void *map)
{
    const uint32_t *source = b->indices;
    uint32_t base = b->index_base;
    if (!plan->count) return;
    if (plan->type == VK_INDEX_TYPE_UINT16) {
        uint16_t *dst = (uint16_t *)map;
        if (b->prim == 8) {
            for (uint32_t q = 0; q < b->index_count / 4; q++) {
                const uint32_t *s = source + (size_t)q * 4;
                uint16_t a = (uint16_t)(s[0] - base), c = (uint16_t)(s[2] - base);
                uint16_t *d = dst + (size_t)q * 6;
                d[0] = a; d[1] = (uint16_t)(s[1] - base); d[2] = c;
                d[3] = a; d[4] = c; d[5] = (uint16_t)(s[3] - base);
            }
        } else {
            nv2a_rebase_indices16(dst, source, b->index_count, base);
            if (b->prim == 3) dst[b->index_count] = (uint16_t)(source[0] - base);
        }
    } else {
        uint32_t *dst = (uint32_t *)map;
        if (b->prim == 8) {
            for (uint32_t q = 0; q < b->index_count / 4; q++) {
                const uint32_t *s = source + (size_t)q * 4;
                uint32_t a = s[0] - base, c = s[2] - base;
                uint32_t *d = dst + (size_t)q * 6;
                d[0] = a; d[1] = s[1] - base; d[2] = c;
                d[3] = a; d[4] = c; d[5] = s[3] - base;
            }
        } else {
            nv2a_rebase_indices32(dst, source, b->index_count, base);
            if (b->prim == 3) dst[b->index_count] = source[0] - base;
        }
    }
}

static int upload_fused_indices(const Nv2aRawBatch *b, const VkIndexUpload *plan)
{
    if (!plan->count) return 0;
    void *map;
    VkDeviceSize at = ring_alloc(plan->bytes, 4, &map);
    if (!map) return 0;
    index_upload_fill(b, plan, map);
    vkCmdBindIndexBuffer(s_cb, s_f->ring, at, plan->type);
    return 1;
}
