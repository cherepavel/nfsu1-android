#define _POSIX_C_SOURCE 200809L
#include "../src/widescreen.h"
#include "../xboxrecomp/src/kernel/video_layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int wide = 1;
int xbox_video_widescreen(void) { return wide; }

int main(void)
{
    unsetenv("RECOMP_HORPLUS");
    unsetenv("RECOMP_ADAPTIVE_WIDESCREEN");
    unsetenv("RECOMP_FE_YAW");
    nfsu_widescreen_configure(NULL);
    assert(xbox_video_adaptive_widescreen());
    assert(nfsu_widescreen_layout().mode == NFSU_WIDESCREEN_ADAPTIVE);
    nfsu_widescreen_configure("");
    assert(xbox_video_adaptive_widescreen());
    nfsu_widescreen_configure("0");
    assert(!xbox_video_adaptive_widescreen());
    assert(nfsu_widescreen_layout().mode == NFSU_WIDESCREEN_LEGACY);
    nfsu_widescreen_configure("invalid");
    assert(!xbox_video_adaptive_widescreen());
    nfsu_widescreen_configure("1");
    assert(xbox_video_adaptive_widescreen());

    const unsigned sizes[][2] = {{640,480}, {1024,768}, {1440,1080},
        {1920,1200}, {1920,1080}, {2400,1080}, {2520,1080}, {3440,1440}};
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        xbox_video_set_output_size(sizes[i][0], sizes[i][1]);
        NfsuWidescreenLayout layout = nfsu_widescreen_layout();
        int native = i < 3;
        assert(layout.mode == (native ? NFSU_WIDESCREEN_NATIVE : NFSU_WIDESCREEN_ADAPTIVE));
        if (native) {
            assert(layout.hud_scale == 1 && layout.edge_shift == 0 && layout.menu_expand == 1);
            /* Native mode ignores even an explicit widescreen yaw override. */
            setenv("RECOMP_FE_YAW", "25", 1);
            assert(nfsu_widescreen_garage_yaw(&layout) == 0);
            unsetenv("RECOMP_FE_YAW");
        }
        for (int fe = 0; fe <= 1; fe++) {
            float x = 1.13f, y = -1.79f;
            nfsu_widescreen_projection(&layout, fe, &x, &y);
            assert(y == -1.79f); /* no garage zoom with increasing width */
            if (native) assert(x == 1.13f);
            else {
                assert(fabsf(fabsf(x / y) * layout.aspect - 1) < 0.00001f);
                assert(fabsf(sizes[i][0] / 640.0f * layout.hud_scale
                             - sizes[i][1] / 480.0f) < 0.00001f);
            }
        }
    }
    /* Resize back to native in the same process; no sticky widescreen. */
    xbox_video_set_output_size(1024, 768);
    assert(nfsu_widescreen_layout().mode == NFSU_WIDESCREEN_NATIVE);

    setenv("RECOMP_ADAPTIVE_WIDESCREEN", "0", 1);
    nfsu_widescreen_configure("1");
    assert(!xbox_video_adaptive_widescreen());
    setenv("RECOMP_ADAPTIVE_WIDESCREEN", "1", 1);
    nfsu_widescreen_configure("0");
    assert(xbox_video_adaptive_widescreen());
    setenv("RECOMP_ADAPTIVE_WIDESCREEN", "", 1);
    nfsu_widescreen_configure("0");
    assert(xbox_video_adaptive_widescreen());
    setenv("RECOMP_ADAPTIVE_WIDESCREEN", "invalid", 1);
    nfsu_widescreen_configure("1");
    assert(!xbox_video_adaptive_widescreen());
    unsetenv("RECOMP_ADAPTIVE_WIDESCREEN");
    setenv("RECOMP_HORPLUS", "0", 1);
    nfsu_widescreen_configure(NULL);
    assert(!xbox_video_adaptive_widescreen());
    assert(nfsu_widescreen_layout().mode == NFSU_WIDESCREEN_NATIVE);
    unsetenv("RECOMP_HORPLUS");
    wide = 0;
    nfsu_widescreen_configure(NULL);
    assert(!xbox_video_adaptive_widescreen());
    assert(nfsu_widescreen_layout().mode == NFSU_WIDESCREEN_NATIVE);
    puts("Widescreen policy: default on, native 4:3, adaptive garage/race, resize and rollback: PASS");
}
