#ifndef NV2A_INDICES_H
#define NV2A_INDICES_H
#include <stdint.h>
#include <string.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

/* ARRAY_ELEMENT16 stores the low index first. Source command memory and the
 * renderer's index array are disjoint. Keep whole pairs at the capacity edge. */
static inline uint32_t nv2a_append_indices16(uint32_t *dst, uint32_t used,
                                           uint32_t capacity,
                                           const uint32_t *src, uint32_t words)
{
    if (used >= capacity) return used;
    uint32_t pairs = (capacity - used) / 2;
    if (pairs > words) pairs = words;
    uint32_t i = 0;
#if defined(__aarch64__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    /* Index storage is 32-bit (DRAW_ARRAYS also supports 24-bit indices).
     * Widen eight packed 16-bit indices at a time, preserving their order. */
    for (; pairs - i >= 4; i += 4) {
        uint16x8_t packed = vreinterpretq_u16_u32(vld1q_u32(src + i));
        vst1q_u32(dst + used + 2 * i, vmovl_u16(vget_low_u16(packed)));
        vst1q_u32(dst + used + 2 * i + 4, vmovl_u16(vget_high_u16(packed)));
    }
#endif
    for (; i < pairs; ++i) {
        dst[used + 2 * i] = (uint16_t)src[i];
        dst[used + 2 * i + 1] = (uint16_t)(src[i] >> 16);
    }
    return used + 2 * pairs;
}
#endif
