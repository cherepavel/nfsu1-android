#ifndef APU_FILTER_NEON_H
#define APU_FILTER_NEON_H
/* SPDX-License-Identifier: LGPL-2.0-or-later
 * SVF equations derived from apu_state.h; original xemu copyrights apply. */
/* Included after apu_state.h. Parallel lanes are independent channels, never
 * consecutive samples: the recursive filter's sample order is unchanged. */
#if defined(__aarch64__) && !defined(APU_FILTER_NO_NEON)
#include <arm_neon.h>
#define APU_FILTER_HAS_NEON 1
_Static_assert(F_R == 1, "paired LPF implements one SVF iteration");
static inline int apu_filter_lowpass_pair(sv_filter sv[2], float samples[][2], unsigned count)
{
    if (!count) return 1;
    float32x2_t f = {sv[0].f, sv[1].f}, q = {sv[0].q, sv[1].q};
    float32x2_t norm = {sv[0].qnrm, sv[1].qnrm};
    float32x2_t b = {sv[0].b, sv[1].b}, l = {sv[0].l, sv[1].l};
    /* Scalar compilers may commute arithmetic operands, choosing a different
     * NaN payload. Reject nonfinite inputs before any state/output write. */
    uint32x2_t exponent = vdup_n_u32(0x7f800000u);
#define APU_NONFINITE(v) vceq_u32(vand_u32(vreinterpret_u32_f32(v), exponent), exponent)
    uint32x2_t bad = vorr_u32(vorr_u32(APU_NONFINITE(f), APU_NONFINITE(q)),
        vorr_u32(APU_NONFINITE(norm), vorr_u32(APU_NONFINITE(b), APU_NONFINITE(l))));
#undef APU_NONFINITE
    uint32x4_t input_bad = vdupq_n_u32(0), mask = vdupq_n_u32(0x7f800000u);
    unsigned at = 0;
    for (; at + 1 < count; at += 2) {
        uint32x4_t bits = vreinterpretq_u32_f32(vld1q_f32(samples[at]));
        input_bad = vorrq_u32(input_bad, vceqq_u32(vandq_u32(bits, mask), mask));
    }
    bad = vorr_u32(bad, vorr_u32(vget_low_u32(input_bad), vget_high_u32(input_bad)));
    if (at < count) {
        uint32x2_t bits = vreinterpret_u32_f32(vld1_f32(samples[at]));
        bad = vorr_u32(bad, vceq_u32(vand_u32(bits, exponent), exponent));
    }
    if (vmaxv_u32(bad)) return 0;
    float32x2_t h, n, p;
    for (unsigned i = 0; i < count; i++) {
        float32x2_t in = vmul_f32(norm, vld1_f32(samples[i]));
        b = vsub_f32(b, vmul_f32(vmul_f32(vmul_f32(b, b), b), vdup_n_f32(0.001f)));
        h = vsub_f32(vsub_f32(in, l), vmul_f32(q, b));
        b = vadd_f32(b, vmul_f32(f, h));
        l = vadd_f32(l, vmul_f32(f, b));
        n = vadd_f32(l, h); p = vsub_f32(l, h);
        float32x2_t out = vminnm_f32(vmaxnm_f32(l, vdup_n_f32(-1.0f)), vdup_n_f32(1.0f));
        vst1_f32(samples[i], out);
    }
    sv[0].b = vget_lane_f32(b, 0); sv[1].b = vget_lane_f32(b, 1);
    sv[0].l = vget_lane_f32(l, 0); sv[1].l = vget_lane_f32(l, 1);
    sv[0].h = vget_lane_f32(h, 0); sv[1].h = vget_lane_f32(h, 1);
    sv[0].n = vget_lane_f32(n, 0); sv[1].n = vget_lane_f32(n, 1);
    sv[0].p = vget_lane_f32(p, 0); sv[1].p = vget_lane_f32(p, 1);
    return 1;
}
#else
#define APU_FILTER_HAS_NEON 0
#endif
#endif
