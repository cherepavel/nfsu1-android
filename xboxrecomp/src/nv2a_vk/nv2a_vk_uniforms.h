/* Included after VsBlock/FsBlock and the renderer state. Reused ranges are
 * immutable until the frame fence; ring generation and buffer identity are
 * checked independently of command-buffer changes (early submissions). */
#include <fenv.h>
static void uniform_vs_fill(VsBlock *vb, const Nv2aRawBatch *b, const VkSurf *s,
    const uint32_t *r)
{
    vb->surf[0] = 2.0f / (float)s->w;
    vb->surf[1] = 2.0f / (float)s->h;
    vb->surf[2] = 1.0f / (float)zmax_of(r);
    vb->surf[3] = 0.5f - 0.5f * (float)s->w / (float)s->pw;
    vb->snap_y = 0.5f - 0.5f * (float)s->h / (float)s->ph;
    memcpy(vb->m, b->composite, sizeof vb->m);
    memcpy(vb->vpoff, b->vp_offset, sizeof vb->vpoff);
    vb->aa[0] = b->aa_sx > 0 ? b->aa_sx : 1.0f;
    vb->aa[1] = b->aa_sy > 0 ? b->aa_sy : 1.0f;
    vb->xform = b->xform == 1 ? 1 : 0;
}

static void uniform_fs_fill(FsBlock *fb, const uint32_t *r, const float tscale[4][2])
{
    memset(fb, 0, sizeof *fb);
    for (unsigned i = 0; i < 4; i++) {
        fb->tscale[i][0] = tscale[i][0];
        fb->tscale[i][1] = tscale[i][1];
    }
    for (unsigned i = 0; i < 8; i++) {
        argb_vec4(r[0xA60 / 4 + i], fb->c0[i]);
        argb_vec4(r[0xA80 / 4 + i], fb->c1[i]);
    }
    argb_vec4(r[0x1E20 / 4], fb->fc0);
    argb_vec4(r[0x1E24 / 4], fb->fc1);
    uint32_t fc = r[0x2A8 / 4];
    fb->fog[0] = byte_f(fc, 0); fb->fog[1] = byte_f(fc, 8);
    fb->fog[2] = byte_f(fc, 16); fb->fog[3] = byte_f(fc, 24);
    fb->afunc = r[0x300 / 4] ? (int32_t)r[0x33C / 4] : 0x207;
    fb->aref = (float)(r[0x340 / 4] & 0xFF);
}

/* Keep a combined builder for rollback and sustained misses in both stages.
 * It avoids entering the two independent key lookups during their cooldown. */
static int uniform_blocks_upload(const Nv2aRawBatch *b, const VkSurf *s,
    const uint32_t *r, const float tscale[4][2], VkDescriptorBufferInfo out[2], size_t *uploaded)
{
    void *map;
    VkDeviceSize at = ring_alloc(sizeof(VsBlock), s_ubo_align, &map);
    if (!map) return 0;
    uniform_vs_fill(map, b, s, r);
    out[0] = (VkDescriptorBufferInfo){s_f->ring, at, sizeof(VsBlock)};
    *uploaded += sizeof(VsBlock);
    at = ring_alloc(sizeof(FsBlock), s_ubo_align, &map);
    if (!map) return 0;
    uniform_fs_fill(map, r, tscale);
    out[1] = (VkDescriptorBufferInfo){s_f->ring, at, sizeof(FsBlock)};
    *uploaded += sizeof(FsBlock);
    return 1;
}

#ifdef __ANDROID__
typedef struct {
    uint32_t w, h, pw, ph, zmax, matrix[16], offset[4], aa[2], xform, fp_mode;
} VkUniformVsKey;
typedef struct {
    uint32_t colors[18], fog, afunc, aref, scales[4][2], fp_mode;
} VkUniformFsKey;
_Static_assert(sizeof(VkUniformVsKey) == 116, "VS key has no padding");
_Static_assert(sizeof(VkUniformFsKey) == 120, "FS key has no padding");
static struct {
    VkUniformVsKey key;
    VkBuffer buffer;
    VkDeviceSize at, end;
    uint32_t ring_gen;
    unsigned misses, skip;
    int valid;
} s_uniform_vs;
static struct {
    VkUniformFsKey key;
    VkBuffer buffer;
    VkDeviceSize at, end;
    uint32_t ring_gen;
    unsigned misses, skip;
    int valid;
} s_uniform_fs;
static struct {
    uint64_t vs_requests, fs_requests, vs_hits, fs_hits, vs_skipped, fs_skipped, uploaded_bytes, reused_bytes;
} s_uniform_stats;

static int uniform_cache_mode(void)
{
    static int mode = -1;
    if (mode < 0) {
        char property[PROP_VALUE_MAX] = {0};
        const char *value = getenv("RECOMP_VK_UNIFORM_CACHE");
        if (!value && __system_property_get("debug.nfsu.uniform_cache", property) > 0)
            value = property;
        mode = !value || !*value || !strcmp(value, "1");
        NFS_LOG_INFO("graphics", "[VKUBO] reuse=%d (startup only)\n", mode);
    }
    return mode;
}

/* Rounding/flush/default-NaN controls can affect rebuilt float bytes. Read
 * FPCR for each probed block on ARM64, so a mode change cannot reuse results.
 * FPSR exception flags do not affect generated values and are not a key. */
static uint32_t uniform_fp_mode(void)
{
#if defined(__aarch64__)
    uint64_t mode;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(mode));
    return (uint32_t)mode;
#else
    return (uint32_t)fegetround();
#endif
}
static int uniform_vs_upload(const Nv2aRawBatch *b, const VkSurf *s,
    const uint32_t *r, VkDescriptorBufferInfo *out)
{
    VkUniformVsKey key;
    int probe = 0;
    s_uniform_stats.vs_requests++;
    if (s_uniform_vs.ring_gen != s_ring_gen || s_uniform_vs.buffer != s_f->ring) {
        s_uniform_vs.valid = 0; s_uniform_vs.misses = s_uniform_vs.skip = 0;
        s_uniform_vs.ring_gen = s_ring_gen; s_uniform_vs.buffer = s_f->ring;
    }
    if (s_uniform_vs.skip) { s_uniform_vs.skip--; s_uniform_stats.vs_skipped++; }
    else probe = 1;
    if (probe) {
        key.w = s->w; key.h = s->h; key.pw = s->pw; key.ph = s->ph; key.zmax = zmax_of(r);
        memcpy(key.matrix, b->composite, sizeof key.matrix);
        memcpy(key.offset, b->vp_offset, sizeof key.offset);
        memcpy(&key.aa[0], &b->aa_sx, 4); memcpy(&key.aa[1], &b->aa_sy, 4);
        key.xform = b->xform == 1; key.fp_mode = uniform_fp_mode();
        if (s_uniform_vs.valid && s_uniform_vs.ring_gen == s_ring_gen
            && s_uniform_vs.buffer == s_f->ring && s_uniform_vs.end <= s_f->ring_off
            && !memcmp(&key, &s_uniform_vs.key, sizeof key)) {
            *out = (VkDescriptorBufferInfo){s_f->ring, s_uniform_vs.at, sizeof(VsBlock)};
            s_uniform_stats.vs_hits++; s_uniform_stats.reused_bytes += sizeof(VsBlock);
            s_uniform_vs.misses = 0;
            return 1;
        }
        /* Sustained misses probe only once per nine draws after warm-up.
         * A hit immediately resumes normal lookup; a new ring resets this. */
        if (++s_uniform_vs.misses >= 4) { s_uniform_vs.misses = 4; s_uniform_vs.skip = 8; }
    }
    void *map;
    VkDeviceSize at = ring_alloc(sizeof(VsBlock), s_ubo_align, &map);
    if (!map) return 0;
    uniform_vs_fill(map, b, s, r);
    *out = (VkDescriptorBufferInfo){s_f->ring, at, sizeof(VsBlock)};
    if (probe) {
        s_uniform_vs.key = key;
        s_uniform_vs.buffer = s_f->ring; s_uniform_vs.ring_gen = s_ring_gen;
        s_uniform_vs.at = at; s_uniform_vs.end = at + sizeof(VsBlock); s_uniform_vs.valid = 1;
    }
    s_uniform_stats.uploaded_bytes += sizeof(VsBlock);
    return 1;
}

static int uniform_fs_upload(const uint32_t *r, const float tscale[4][2],
    VkDescriptorBufferInfo *out)
{
    VkUniformFsKey key;
    int probe = 0;
    s_uniform_stats.fs_requests++;
    if (s_uniform_fs.ring_gen != s_ring_gen || s_uniform_fs.buffer != s_f->ring) {
        s_uniform_fs.valid = 0; s_uniform_fs.misses = s_uniform_fs.skip = 0;
        s_uniform_fs.ring_gen = s_ring_gen; s_uniform_fs.buffer = s_f->ring;
    }
    if (s_uniform_fs.skip) { s_uniform_fs.skip--; s_uniform_stats.fs_skipped++; }
    else probe = 1;
    if (probe) {
        memcpy(key.colors, r + 0xA60 / 4, 8 * 4);
        memcpy(key.colors + 8, r + 0xA80 / 4, 8 * 4);
        key.colors[16] = r[0x1E20 / 4]; key.colors[17] = r[0x1E24 / 4];
        key.fog = r[0x2A8 / 4]; key.afunc = r[0x300 / 4] ? r[0x33C / 4] : 0x207;
        key.aref = r[0x340 / 4] & 0xFF;
        memcpy(key.scales, tscale, sizeof key.scales); key.fp_mode = uniform_fp_mode();
        if (s_uniform_fs.valid && s_uniform_fs.ring_gen == s_ring_gen
            && s_uniform_fs.buffer == s_f->ring && s_uniform_fs.end <= s_f->ring_off
            && !memcmp(&key, &s_uniform_fs.key, sizeof key)) {
            *out = (VkDescriptorBufferInfo){s_f->ring, s_uniform_fs.at, sizeof(FsBlock)};
            s_uniform_stats.fs_hits++; s_uniform_stats.reused_bytes += sizeof(FsBlock);
            s_uniform_fs.misses = 0;
            return 1;
        }
        if (++s_uniform_fs.misses >= 4) { s_uniform_fs.misses = 4; s_uniform_fs.skip = 8; }
    }
    void *map;
    VkDeviceSize at = ring_alloc(sizeof(FsBlock), s_ubo_align, &map);
    if (!map) return 0;
    uniform_fs_fill(map, r, tscale);
    *out = (VkDescriptorBufferInfo){s_f->ring, at, sizeof(FsBlock)};
    if (probe) {
        s_uniform_fs.key = key;
        s_uniform_fs.buffer = s_f->ring; s_uniform_fs.ring_gen = s_ring_gen;
        s_uniform_fs.at = at; s_uniform_fs.end = at + sizeof(FsBlock); s_uniform_fs.valid = 1;
    }
    s_uniform_stats.uploaded_bytes += sizeof(FsBlock);
    return 1;
}
#endif

static int uniform_upload(const Nv2aRawBatch *b, const VkSurf *s, const uint32_t *r,
    const float tscale[4][2], VkDescriptorBufferInfo out[2])
{
#ifdef __ANDROID__
    int mode = uniform_cache_mode();
    if (mode && s_uniform_vs.skip && s_uniform_fs.skip
        && s_uniform_vs.ring_gen == s_ring_gen && s_uniform_fs.ring_gen == s_ring_gen
        && s_uniform_vs.buffer == s_f->ring && s_uniform_fs.buffer == s_f->ring) {
        s_uniform_stats.vs_requests++; s_uniform_stats.fs_requests++;
        s_uniform_stats.vs_skipped++; s_uniform_stats.fs_skipped++;
        s_uniform_vs.skip--; s_uniform_fs.skip--;
        size_t uploaded = 0;
        int ok = uniform_blocks_upload(b, s, r, tscale, out, &uploaded);
        s_uniform_stats.uploaded_bytes += uploaded;
        return ok;
    }
    if (mode) return uniform_vs_upload(b, s, r, &out[0])
                  && uniform_fs_upload(r, tscale, &out[1]);
#endif
    size_t uploaded = 0;
    return uniform_blocks_upload(b, s, r, tscale, out, &uploaded);
}
