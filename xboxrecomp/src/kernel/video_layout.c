#include "video_layout.h"
#include <stdatomic.h>
#include <math.h>

static atomic_int adaptive;
static _Atomic uint64_t output_size = (UINT64_C(1280) << 32) | 720;

void xbox_video_set_adaptive_widescreen(int enabled)
{
    atomic_store_explicit(&adaptive, !!enabled, memory_order_relaxed);
}

int xbox_video_adaptive_widescreen(void)
{
    return atomic_load_explicit(&adaptive, memory_order_relaxed);
}

void xbox_video_set_output_size(uint32_t width, uint32_t height)
{
    if (width && height)
        atomic_store_explicit(&output_size, ((uint64_t)width << 32) | height,
                              memory_order_relaxed);
}

float xbox_video_output_aspect(void)
{
    if (!xbox_video_adaptive_widescreen()) return 16.0f / 9.0f;
    uint64_t size = atomic_load_explicit(&output_size, memory_order_relaxed);
    return (float)(size >> 32) / (float)(uint32_t)size;
}

XboxVideoRect xbox_video_output_rect(float lw, float lh,
                                    uint32_t width, uint32_t height, int wide)
{
    XboxVideoRect rect = {0};
    if (!isfinite(lw) || !isfinite(lh) || !(lw > 0) || !(lh > 0) || !width || !height)
        return rect;
    if (xbox_video_adaptive_widescreen()) {
        rect.width = width;
        rect.height = height;
        return rect;
    }
    if (wide && lw < lh * 1.5f) lw = lh * 16.0f / 9.0f;
    float k = width / lw < height / lh ? width / lw : height / lh;
    rect.width = (uint32_t)(lw * k);
    rect.height = (uint32_t)(lh * k);
    rect.x = ((int32_t)width - (int32_t)rect.width) / 2;
    rect.y = ((int32_t)height - (int32_t)rect.height) / 2;
    return rect;
}
