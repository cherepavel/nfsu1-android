#ifndef NV2A_VK_INDEXED_H
#define NV2A_VK_INDEXED_H
#include <stdint.h>
#include <stddef.h>
#if defined(__aarch64__) && !defined(NV2A_VK_INDEXED_NO_NEON)
#include <arm_neon.h>
#define NV2A_VK_INDEXED_NEON 1

/* Low Morton bits in one 4x4 quadrant: x occupies bits 0/2, y bits 1/3.
 * Scalar palette gathers preserve all color bits; NEON stores four pixels. */
static inline void nv2a_vk_indexed_quad(const uint8_t *p, const uint32_t *pal,
    uint32_t *out, size_t stride)
{
    vst1q_u32(out, ((uint32x4_t){pal[p[0]], pal[p[1]], pal[p[4]], pal[p[5]]})); out += stride;
    vst1q_u32(out, ((uint32x4_t){pal[p[2]], pal[p[3]], pal[p[6]], pal[p[7]]})); out += stride;
    vst1q_u32(out, ((uint32x4_t){pal[p[8]], pal[p[9]], pal[p[12]], pal[p[13]]})); out += stride;
    vst1q_u32(out, ((uint32x4_t){pal[p[10]], pal[p[11]], pal[p[14]], pal[p[15]]}));
}
#else
#define NV2A_VK_INDEXED_NEON 0
#endif

/* Use measured dimensions only. A rejected shape leaves out untouched, so
 * the caller can run its original scalar loop. Tables are the existing full
 * x/y Morton offsets; each aligned 8x8 tile occupies exactly 64 source bytes. */
static inline int nv2a_vk_indexed_tiles(const uint8_t *idx, uint32_t w, uint32_t h,
    const uint32_t pal[256], const uint32_t *sx, const uint32_t *sy, uint32_t *out)
{
#if NV2A_VK_INDEXED_NEON
    if (w < 8 || h < 8 || w > 512 || h > 512 || (w & (w - 1)) || (h & (h - 1)))
        return 0;
    for (uint32_t y = 0; y < h; y += 8) {
        for (uint32_t x = 0; x < w; x += 8) {
            const uint8_t *p = idx + (sx[x] | sy[y]);
            uint32_t *o = out + (size_t)y * w + x;
            nv2a_vk_indexed_quad(p, pal, o, w);
            nv2a_vk_indexed_quad(p + 16, pal, o + 4, w);
            nv2a_vk_indexed_quad(p + 32, pal, o + (size_t)4 * w, w);
            nv2a_vk_indexed_quad(p + 48, pal, o + (size_t)4 * w + 4, w);
        }
    }
    return 1;
#else
    (void)idx; (void)w; (void)h; (void)pal; (void)sx; (void)sy; (void)out;
    return 0;
#endif
}
#endif
