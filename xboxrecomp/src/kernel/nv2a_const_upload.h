#ifndef NV2A_CONST_UPLOAD_H
#define NV2A_CONST_UPLOAD_H
#include <stdint.h>
#include <string.h>
#if defined(__aarch64__) && !defined(NV2A_CONST_NO_NEON)
#include <arm_neon.h>
#endif

#define NV2A_VP_CONSTANTS 192

/* The source is guest command memory, disjoint from the private register
 * shadow. Each dirty byte covers 16 dwords. Preserve existing dirty values
 * for unchanged blocks; changed blocks become exactly 1, like scalar writes.
 * Only the command executor owns this state; no observer runs within a run. */
static inline void nv2a_shadow_write_run(uint32_t *registers, uint8_t *dirty,
    uint32_t first, const uint32_t *source, uint32_t count)
{
    while (count) {
        uint32_t words = 16 - (first & 15);
        if (words > count) words = count;
        uint32_t i = 0, changed = 0;
#if defined(__aarch64__) && !defined(NV2A_CONST_NO_NEON)
        uint32x4_t differences = vdupq_n_u32(0);
        for (; words - i >= 4; i += 4) {
            uint32x4_t next = vld1q_u32(source + i);
            differences = vorrq_u32(differences, veorq_u32(vld1q_u32(registers + first + i), next));
            vst1q_u32(registers + first + i, next);
        }
        changed = vmaxvq_u32(differences);
#endif
        for (; i < words; i++) {
            changed |= registers[first + i] ^ source[i];
            registers[first + i] = source[i];
        }
        if (changed) dirty[first >> 4] = 1;
        first += words; source += words; count -= words;
    }
}

/* An incrementing SET_TRANSFORM_CONSTANT packet has at most 32 dwords.
 * Its method selects the starting component, not a vector offset from LOAD.
 * Copy valid contiguous bytes, including arbitrary float/NaN bit patterns.
 * Invalid LOAD values discard writes until their uint32 vector index wraps. */
static inline uint32_t nv2a_constant_write_run(void *constants, uint32_t load,
    uint32_t component, const uint32_t *source, uint32_t count)
{
    if (load < NV2A_VP_CONSTANTS) {
        uint32_t available = (NV2A_VP_CONSTANTS - load) * 4 - component;
        uint32_t words = count < available ? count : available;
        if (words) memcpy((uint8_t *)constants + (size_t)load * 16 + component * 4,
                          source, (size_t)words * 4);
    } else {
        uint64_t until_wrap = ((UINT64_C(1) << 32) - load) * 4 - component;
        if (until_wrap < count) {
            uint32_t skipped = (uint32_t)until_wrap;
            memcpy(constants, source + skipped, (size_t)(count - skipped) * 4);
        }
    }
    return load + (component + count) / 4;
}
#endif
