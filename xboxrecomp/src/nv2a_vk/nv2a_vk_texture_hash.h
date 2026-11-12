#ifndef NV2A_VK_TEXTURE_HASH_H
#define NV2A_VK_TEXTURE_HASH_H
#include <stdint.h>
#include <string.h>

/* The rounded stride yields at most 511 loop samples plus the final word. */
#define NV2A_VK_HASH_SAMPLES 512
#define NV2A_VK_PALETTE_SAMPLES 257
#define NV2A_VK_SNAPSHOT_MIN_BYTES 1024
#define NV2A_VK_SNAPSHOT_MAX_BYTES 4096
typedef struct {
    uint32_t bytes, paletted, hash;
    uint32_t data[NV2A_VK_HASH_SAMPLES];
    uint32_t palette[NV2A_VK_PALETTE_SAMPLES];
} VkTextureHashSnapshot;

static inline uint32_t nv2a_vk_texture_hash_capture(const uint8_t *mem,
    uint32_t bytes, uint32_t *samples)
{
    uint32_t hash = 2166136261u ^ bytes, at = 0;
    if (!bytes) return hash;
    uint32_t step = bytes / 256;
    if (step < 4) step = 4;
    step &= ~3u;
    for (uint64_t i = 0; i + 4 <= bytes; i += step) {
        uint32_t word;
        memcpy(&word, mem + i, 4);
        samples[at++] = word;
        hash = (hash ^ word) * 16777619u;
    }
    uint32_t last;
    memcpy(&last, mem + bytes - 4, 4);
    samples[at] = last;
    return (hash ^ last) * 16777619u;
}

/* Compare every legacy sample, including the final word. Four independent
 * differences avoid the serial FNV multiply dependency on unchanged input.
 * A match implies the exact legacy hash is unchanged. A mismatch makes no
 * claim about hash identity: the caller must still run the legacy hash. */
static inline int nv2a_vk_texture_hash_matches(const uint8_t *mem, uint32_t bytes,
    const uint32_t *samples)
{
    if (!bytes) return 1;
    uint32_t step = bytes / 256;
    if (step < 4) step = 4;
    step &= ~3u;
    uint64_t i = 0, stride = step;
    uint32_t at = 0, d0 = 0, d1 = 0, d2 = 0, d3 = 0;
    if (bytes >= 4) {
        uint64_t limit = bytes - 4;
        for (; i + 3 * stride <= limit; i += 4 * stride, at += 4) {
            uint32_t a, b, c, d;
            memcpy(&a, mem + i, 4);
            memcpy(&b, mem + i + stride, 4);
            memcpy(&c, mem + i + 2 * stride, 4);
            memcpy(&d, mem + i + 3 * stride, 4);
            d0 |= a ^ samples[at]; d1 |= b ^ samples[at + 1];
            d2 |= c ^ samples[at + 2]; d3 |= d ^ samples[at + 3];
        }
        for (; i <= limit; i += stride, at++) {
            uint32_t word;
            memcpy(&word, mem + i, 4); d0 |= word ^ samples[at];
        }
    }
    uint32_t last;
    memcpy(&last, mem + bytes - 4, 4);
    return !(d0 | d1 | d2 | d3 | (last ^ samples[at]));
}

#endif
