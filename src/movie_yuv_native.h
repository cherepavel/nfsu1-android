#ifndef NFSU_MOVIE_YUV_NATIVE_H
#define NFSU_MOVIE_YUV_NATIVE_H

/* Included after recomp_types.h with RECOMP_GENERATED_CODE. The three
 * guest tables already contain the game's coefficients and channel order. */
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

static int movie_yuv_span(uint32_t address, uint32_t bytes)
{
    uint32_t low = address & 0x7FFFFFFFu;
    return low >= 0x10000u && low < 0x04000000u
        && bytes <= 0x04000000u - low;
}

static int movie_yuv_overlap(uint32_t a, uint32_t an, uint32_t b, uint32_t bn)
{
    a &= 0x7FFFFFFFu;
    b &= 0x7FFFFFFFu;
    return a < b + bn && b < a + an;
}

static int movie_yuv_try_native(void)
{
    uint32_t stack = g_esp;
    if ((stack & 3u) || !movie_yuv_span(stack, 24)
            || !movie_yuv_span(stack - 16u, 16)) return 0;
    uint32_t y = MEM32(stack + 4), u = MEM32(stack + 8);
    uint32_t v = MEM32(stack + 12), out = MEM32(stack + 16);
    uint32_t end = MEM32(stack + 20), bytes = end - out;
    /* The original is a do/while. Empty, wrapped, and non-pair spans must
     * retain its behavior via the generated implementation. */
    if (end <= out || (bytes & 7u) || !movie_yuv_span(out, bytes)
            || !movie_yuv_span(y, bytes / 4) || !movie_yuv_span(u, bytes / 8)
            || !movie_yuv_span(v, bytes / 8)) return 0;
    /* Do not change the original read/write ordering for aliased buffers. */
    if (movie_yuv_overlap(out, bytes, y, bytes / 4)
            || movie_yuv_overlap(out, bytes, u, bytes / 8)
            || movie_yuv_overlap(out, bytes, v, bytes / 8)
            || movie_yuv_overlap(out, bytes, 0x25CC30u, 3 * 2048)
            || movie_yuv_overlap(out, bytes, stack - 16, 40)
            || movie_yuv_overlap(y, bytes / 4, stack - 16, 16)
            || movie_yuv_overlap(u, bytes / 8, stack - 16, 16)
            || movie_yuv_overlap(v, bytes / 8, stack - 16, 16)) return 0;

    /* Preserve the four original push writes, including the FPO frame. */
    MEM32(stack - 4) = g_ebx;
    MEM32(stack - 8) = g_esi;
    MEM32(stack - 12) = g_edi;
    MEM32(stack - 16) = g_seh_ebp;
    const uint8_t *yp = (const uint8_t *)XBOX_PTR(y);
    const uint8_t *up = (const uint8_t *)XBOX_PTR(u);
    const uint8_t *vp = (const uint8_t *)XBOX_PTR(v);
    uint8_t *dst = (uint8_t *)XBOX_PTR(out);
    uint32_t pairs = bytes / 8;
#if defined(__ARM_NEON)
    const int16_t *yt = (const int16_t *)XBOX_PTR(0x25CC30u);
    const int16_t *ut = (const int16_t *)XBOX_PTR(0x25D430u);
    const int16_t *vt = (const int16_t *)XBOX_PTR(0x25DC30u);
    int16x4_t chroma, vrow, second;
    uint8x8_t packed;
    for (uint32_t i = 0; i < pairs; i++) {
        vrow = vld1_s16(vt + vp[i] * 4u);
        chroma = vadd_s16(vld1_s16(ut + up[i] * 4u), vrow);
        int16x4_t first = vadd_s16(vld1_s16(yt + yp[i * 2] * 4u), chroma);
        second = vadd_s16(vld1_s16(yt + yp[i * 2 + 1] * 4u), chroma);
        /* PADDW wraps at 16 bits; PACKUSWB saturates signed words to bytes. */
        packed = vqmovun_s16(vcombine_s16(first, second));
        vst1_u8(dst + i * 8u, packed);
    }
    vst1_u8(g_mm0.ub, packed);
    vst1_s16(g_mm1.w, second);
    vst1_s16(g_mm2.w, chroma);
    vst1_s16(g_mm3.w, vrow);
#else
    for (uint32_t i = 0; i < pairs; i++) {
        g_mm3 = MMX_MEM(0x25DC30u + vp[i] * 8u);
        g_mm2 = MMX_PADDW(MMX_MEM(0x25D430u + up[i] * 8u), g_mm3);
        g_mm0 = MMX_PADDW(MMX_MEM(0x25CC30u + yp[i * 2] * 8u), g_mm2);
        g_mm1 = MMX_PADDW(MMX_MEM(0x25CC30u + yp[i * 2 + 1] * 8u), g_mm2);
        g_mm0 = MMX_PACKUSWB(g_mm0, g_mm1);
        memcpy(dst + i * 8u, &g_mm0, 8);
    }
#endif
    g_eax = 0;
    g_ecx = y + bytes / 4;
    g_edx = u + pairs;
    g_esp = stack + 4; /* cdecl: arguments remain for the caller */
    return 1;
}
#endif
