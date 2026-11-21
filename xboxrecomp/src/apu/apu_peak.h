#ifndef APU_PEAK_H
#define APU_PEAK_H
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#if defined(__aarch64__) && !defined(APU_PEAK_NO_NEON)
#include <arm_neon.h>
#define APU_PEAK_HAS_NEON 1
#else
#define APU_PEAK_HAS_NEON 0
#endif

/* The scalar abs/greater-than/select policy ignores NaNs and retains a NaN
 * seed, equal maxima and signed zero. The input samples stay read-only. */
#define APU_PEAK_ROW_SAMPLES 32
#if APU_PEAK_HAS_NEON
static inline float32x4_t apu_peak_max(float32x4_t a, float32x4_t b)
{
    return vbslq_f32(vcgtq_f32(b, a), b, a);
}
#endif
static inline float apu_peak_abs_rows(const float samples[][APU_PEAK_ROW_SAMPLES],
                                      size_t rows, float peak)
{
#if APU_PEAK_HAS_NEON
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0,fpcr" : "=r"(fpcr));
    /* FZ makes subnormals compare equal to zero. The original loop retains
     * the first representation of such ties, so preserve its sample order. */
    if (!(fpcr & (1ull << 24))) {
        float32x4_t m0 = vdupq_n_f32(peak), m1 = m0, m2 = m0, m3 = m0;
        for (size_t row = 0; row < rows; row++)
            for (size_t i = 0; i < APU_PEAK_ROW_SAMPLES; i += 16) {
                m0 = apu_peak_max(m0, vabsq_f32(vld1q_f32(samples[row] + i)));
                m1 = apu_peak_max(m1, vabsq_f32(vld1q_f32(samples[row] + i + 4)));
                m2 = apu_peak_max(m2, vabsq_f32(vld1q_f32(samples[row] + i + 8)));
                m3 = apu_peak_max(m3, vabsq_f32(vld1q_f32(samples[row] + i + 12)));
            }
        float32x4_t maxima = apu_peak_max(apu_peak_max(m0, m1), apu_peak_max(m2, m3));
        float lanes[4]; vst1q_f32(lanes, maxima);
        for (unsigned lane = 0; lane < 4; lane++)
            if (lanes[lane] > peak) peak = lanes[lane];
        return peak;
    }
#endif
    for (size_t row = 0; row < rows; row++)
        for (size_t i = 0; i < APU_PEAK_ROW_SAMPLES; i++) {
            float a = fabsf(samples[row][i]);
            if (a > peak) peak = a;
        }
    return peak;
}
#endif
