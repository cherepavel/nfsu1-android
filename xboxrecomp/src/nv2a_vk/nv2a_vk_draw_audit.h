/* One-frame draw fingerprints. The dump contains no vertex or texture data. */
static FILE *s_draw_audit;
static unsigned s_draw_audit_span;
static uint64_t s_draw_audit_vertices, s_draw_audit_copied;
static int s_draw_audit_direct_only;

static uint64_t draw_audit_hash(uint64_t hash, const void *data, size_t size)
{
    const uint8_t *src = data;
    const uint64_t prime = 1099511628211ull;
    hash = (hash ^ size) * prime;
    while (size >= sizeof(uint64_t)) {
        uint64_t word;
        memcpy(&word, src, sizeof word);
        hash = (hash ^ word) * prime;
        src += sizeof word;
        size -= sizeof word;
    }
    if (size) {
        uint64_t tail = 0;
        memcpy(&tail, src, size);
        hash = (hash ^ tail) * prime;
    }
    return hash;
}

static void draw_audit_flip(uint32_t frame, int instancing)
{
    static char last[PROP_VALUE_MAX];
    if (s_draw_audit) {
        fputs("end\n", s_draw_audit);
        int ok = !ferror(s_draw_audit);
        if (fclose(s_draw_audit) != 0) ok = 0;
        s_draw_audit = NULL;
        NFS_LOG_INFO("graphics", "[VKDRAWAUDIT] frame=%u written=%d\n", frame, ok);
    }
    if (frame % 60 || instancing != 0) return;
    char request[PROP_VALUE_MAX] = {0};
    if (__system_property_get("debug.nfsu.draw_audit", request) <= 0
        || !strcmp(request, "0") || !strcmp(request, last))
        return;
    memcpy(last, request, strlen(request) + 1);
    s_draw_audit = fopen("logs/vulkan-draw-audit.tsv", "w");
    if (!s_draw_audit) {
        NFS_LOG_WARN("graphics", "[VKDRAWAUDIT] could not open capture file\n");
        return;
    }
    s_draw_audit_span = 0;
    fprintf(s_draw_audit, "frame\t%u\n", frame);
    fputs("# draw target span pipeline state uniforms vertices geometry copied index_count xform\n",
          s_draw_audit);
}

static void draw_audit_record(const Nv2aRawBatch *b, const VkSurf *surf,
    VkPipeline pipe, const VkDescriptorImageInfo img[4], const VkDescriptorBufferInfo ub[3],
    uint32_t index_count)
{
    const uint32_t *r = b->regs;
    uint64_t geometry = s_draw_audit_direct_only ? s_draw_audit_vertices : 0;
    if (geometry) {
        const uint32_t shape[] = { b->prim, b->index_base, b->index_count };
        geometry = draw_audit_hash(geometry, shape, sizeof shape);
        geometry = draw_audit_hash(geometry, b->indices, (size_t)b->index_count * sizeof *b->indices);
        geometry = draw_audit_hash(geometry, b->attr_const, sizeof b->attr_const);
    }
    const uint32_t dynamic[] = {
        r[0x34C / 4], r[0x36C / 4] & 255, r[0x360 / 4] & 255, r[0x368 / 4] & 255,
        (uint32_t)s_sc_cur.offset.x, (uint32_t)s_sc_cur.offset.y,
        s_sc_cur.extent.width, s_sc_cur.extent.height,
    };
    uint64_t state = draw_audit_hash(14695981039346656037ull, dynamic, sizeof dynamic);
    for (unsigned i = 0; i < 4; i++) {
        const uint64_t binding[] = { (uintptr_t)img[i].sampler, (uintptr_t)img[i].imageView,
                                    img[i].imageLayout };
        state = draw_audit_hash(state, binding, sizeof binding);
    }
    uint64_t uniforms = 14695981039346656037ull;
    for (unsigned i = 0; i < 2; i++) {
        if (ub[i].buffer != s_f->ring || ub[i].offset > RING_BYTES
            || ub[i].range > RING_BYTES - ub[i].offset) {
            uniforms = 0;
            break;
        }
        uniforms = draw_audit_hash(uniforms, s_f->ring_ptr + ub[i].offset, (size_t)ub[i].range);
    }
    fprintf(s_draw_audit, "draw\t%08x\t%u\t%llx\t%llx\t%llx\t%llx\t%llx\t%llu\t%u\t%u\n",
            surf->va, s_draw_audit_span, (unsigned long long)(uintptr_t)pipe,
            (unsigned long long)state, (unsigned long long)uniforms,
            (unsigned long long)s_draw_audit_vertices, (unsigned long long)geometry,
            (unsigned long long)s_draw_audit_copied, index_count, b->xform);
}
