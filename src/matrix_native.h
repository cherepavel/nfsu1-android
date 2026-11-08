#ifndef NFSU_MATRIX_NATIVE_H
#define NFSU_MATRIX_NATIVE_H

/* Included after recomp_types.h. Ordinary guest RAM only: do not coalesce
 * reads of MMIO or change the behavior of invalid/wrapped pointers. */
static int matrix_native_span(uint32_t address, uint32_t size)
{
    return address >= 0x10000u && address < 0x04000000u
        && size <= 0x04000000u - address;
}

#if defined(__aarch64__)
#include <arm_neon.h>

static float32x4_t matrix_native_row(float32x4_t a,
        float32x4_t b0, float32x4_t b1, float32x4_t b2, float32x4_t b3)
{
    float32x4_t r = vmulq_f32(vdupq_laneq_f32(a, 0), b0);
    r = vaddq_f32(r, vmulq_f32(vdupq_laneq_f32(a, 1), b1));
    r = vaddq_f32(r, vmulq_f32(vdupq_laneq_f32(a, 2), b2));
    return vaddq_f32(r, vmulq_f32(vdupq_laneq_f32(a, 3), b3));
}

static int matrix_try_native(void)
{
    uint32_t stack = g_esp;
    if ((stack & 3u) || !matrix_native_span(stack, 16)) return 0;
    uint32_t a = MEM32(stack + 8), b = MEM32(stack + 12);
    uint32_t dst = MEM32(stack + 4);
    if (!matrix_native_span(a, 64) || !matrix_native_span(b, 64)
            || !matrix_native_span(dst, 64)) return 0;

    /* All inputs precede all output stores in the Xbox routine. Snapshot
     * them before writing, including when dst overlaps either matrix. */
    const float *ap = (const float *)XBOX_PTR(a);
    const float *bp = (const float *)XBOX_PTR(b);
    float32x4_t a0 = vld1q_f32(ap), a1 = vld1q_f32(ap + 4);
    float32x4_t a2 = vld1q_f32(ap + 8), a3 = vld1q_f32(ap + 12);
    float32x4_t b0 = vld1q_f32(bp), b1 = vld1q_f32(bp + 4);
    float32x4_t b2 = vld1q_f32(bp + 8), b3 = vld1q_f32(bp + 12);
    /* NaN payload selection can depend on scalar operand allocation in the
     * lifted C. Retain that path for non-finite inputs; inspecting exponent
     * bits here cannot raise FP exceptions or alter guest-visible state. */
    uint32x4_t mask = vdupq_n_u32(0x7F800000u);
    uint32x4_t exponents = vandq_u32(vreinterpretq_u32_f32(a0), mask);
#define MATRIX_EXP(v) exponents = vmaxq_u32(exponents, vandq_u32(vreinterpretq_u32_f32(v), mask))
    MATRIX_EXP(a1); MATRIX_EXP(a2); MATRIX_EXP(a3);
    MATRIX_EXP(b0); MATRIX_EXP(b1); MATRIX_EXP(b2); MATRIX_EXP(b3);
#undef MATRIX_EXP
    if (vmaxvq_u32(exponents) == 0x7F800000u) return 0;
    float32x4_t r0 = matrix_native_row(a0, b0, b1, b2, b3);
    float32x4_t r1 = matrix_native_row(a1, b0, b1, b2, b3);
    float32x4_t r2 = matrix_native_row(a2, b0, b1, b2, b3);
    float32x4_t r3 = matrix_native_row(a3, b0, b1, b2, b3);

    /* XMM registers are guest-visible even though this is a leaf routine. */
    vst1q_f32(g_xmm0.f, vmulq_f32(vdupq_laneq_f32(a3, 2), b2));
    vst1q_f32(g_xmm1.f, vmulq_f32(vdupq_laneq_f32(a3, 3), b3));
    vst1q_f32(g_xmm2.f, r0); vst1q_f32(g_xmm3.f, r1);
    vst1q_f32(g_xmm4.f, r2); vst1q_f32(g_xmm5.f, r3);
    float *out = (float *)XBOX_PTR(dst);
    vst1q_f32(out, r0); vst1q_f32(out + 4, r1);
    vst1q_f32(out + 8, r2); vst1q_f32(out + 12, r3);
    g_ecx = dst;
    /* The output can overlap the stack, so reload after the stores. */
    g_eax = MEM32(stack + 4);
    g_esp = stack + 16; /* ret 12 */
    return 1;
}
#else
static int matrix_try_native(void) { return 0; }
#endif
#endif
