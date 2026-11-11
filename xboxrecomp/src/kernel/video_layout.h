#ifndef XBOX_VIDEO_LAYOUT_H
#define XBOX_VIDEO_LAYOUT_H

#include <stdint.h>

typedef struct {
    int32_t x, y;
    uint32_t width, height;
} XboxVideoRect;

/* Configure before starting guest/render threads. Off unless opted in by
 * the host; the title must also adapt its projection and UI to this mode. */
void xbox_video_set_adaptive_widescreen(int enabled);
int xbox_video_adaptive_widescreen(void);

/* Upright output dimensions, never the pre-rotated Vulkan buffer size.
 * Publish both axes atomically; invalid/lost surfaces retain the last size. */
void xbox_video_set_output_size(uint32_t width, uint32_t height);
float xbox_video_output_aspect(void);

/* Shared by presentation, output-sized UI and inverse touch mapping.
 * Legacy mode keeps the original anamorphic 16:9/4:3 letterbox fit. */
XboxVideoRect xbox_video_output_rect(float logical_width, float logical_height,
                                    uint32_t width, uint32_t height, int wide);

#endif
