/* cc -std=c11 -Wall -Wextra -Werror tests/video_layout.c
 * xboxrecomp/src/kernel/video_layout.c -pthread -lm -o /tmp/nfsu-video-layout */
#include "../xboxrecomp/src/kernel/video_layout.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>

static void *resize(void *unused)
{
    (void)unused;
    for (unsigned i = 0; i < 100000; i++)
        if (i & 1) xbox_video_set_output_size(2400, 1080);
        else xbox_video_set_output_size(1920, 1200);
    return NULL;
}

int main(void)
{
    assert(!xbox_video_adaptive_widescreen());
    XboxVideoRect rect = xbox_video_output_rect(640, 480, 2400, 1080, 1);
    assert(rect.x == 240 && rect.y == 0 && rect.width == 1920 && rect.height == 1080);
    rect = xbox_video_output_rect(640, 480, 2400, 1080, 0);
    assert(rect.x == 480 && rect.y == 0 && rect.width == 1440 && rect.height == 1080);
    xbox_video_set_adaptive_widescreen(1);
    const unsigned sizes[][2] = {
        {640, 480}, {1920, 1200}, {1280, 720}, {2160, 1080}, {2400, 1080},
        {2520, 1080}, {3440, 1440}, {2399, 1081}, {1080, 2400}
    };
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        unsigned w = sizes[i][0], h = sizes[i][1];
        xbox_video_set_output_size(w, h);
        assert(fabsf(xbox_video_output_aspect() - (float)w / h) < 0.000001f);
        for (unsigned aa = 1; aa <= 2; aa++) {
            rect = xbox_video_output_rect(640 * aa / (float)aa, 480, w, h, 1);
            assert(rect.x == 0 && rect.y == 0 && rect.width == w && rect.height == h);
        }
        xbox_video_set_output_size(0, 0);
        assert(fabsf(xbox_video_output_aspect() - (float)w / h) < 0.000001f);
    }
    xbox_video_set_output_size(2400, 1080);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, resize, NULL));
    for (unsigned i = 0; i < 100000; i++) {
        float aspect = xbox_video_output_aspect();
        /* No mixed width/height snapshot while a surface is resized. */
        assert(aspect == 2400.0f / 1080 || aspect == 1920.0f / 1200);
    }
    assert(!pthread_join(thread, NULL));
    xbox_video_set_adaptive_widescreen(0);
    assert(xbox_video_output_aspect() == 16.0f / 9.0f);
    rect = xbox_video_output_rect(640, 480, 2400, 1080, 1);
    assert(rect.x == 240 && rect.width == 1920 && rect.height == 1080);
    puts("Video layout: full output, legacy rollback, resize and atomic dimensions: PASS");
}
