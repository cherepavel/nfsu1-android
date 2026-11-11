#include "widescreen.h"
#include "../xboxrecomp/src/kernel/video_layout.h"
#include "../xboxrecomp/include/nfsu_log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern int xbox_video_widescreen(void);

static int corrections_enabled(void)
{
    const char *horplus = getenv("RECOMP_HORPLUS");
    return xbox_video_widescreen() && !(horplus && horplus[0] == '0');
}

void nfsu_widescreen_configure(const char *property)
{
    const char *value = getenv("RECOMP_ADAPTIVE_WIDESCREEN");
    if (!value) value = property;
    /* Unset/empty selects the packaged default. Explicit 0 (and malformed
     * nonempty values) retains the previous presentation for rollback. */
    int requested = !value || !*value || !strcmp(value, "1");
    int enabled = requested && corrections_enabled();
    xbox_video_set_adaptive_widescreen(enabled);
    NFS_LOG_INFO("graphics", "[WIDE] adaptive=%d requested=%d (startup)\n", enabled, requested);
}

NfsuWidescreenLayout nfsu_widescreen_layout(void)
{
    NfsuWidescreenLayout layout = {NFSU_WIDESCREEN_NATIVE, 4.0f / 3.0f, 1, 0, 1};
    if (!corrections_enabled()) return layout;
    if (xbox_video_adaptive_widescreen()) {
        layout.aspect = xbox_video_output_aspect();
        /* Auto-widescreen is needed only beyond the original 4:3 frame.
         * Keep the native camera, HUD and garage angle at 4:3. */
        if (layout.aspect <= 4.0f / 3.0f) return layout;
        layout.mode = NFSU_WIDESCREEN_ADAPTIVE;
        layout.hud_scale = (4.0f / 3.0f) / layout.aspect;
        layout.edge_shift = 240.0f * layout.aspect - 320.0f;
        layout.menu_expand = 1.0f / layout.hud_scale;
    } else {
        layout.mode = NFSU_WIDESCREEN_LEGACY;
        layout.aspect = 16.0f / 9.0f;
        layout.hud_scale = 0.75f;
        layout.edge_shift = 640.0f / 6.0f;
        layout.menu_expand = 4.0f / 3.0f;
    }
    return layout;
}

void nfsu_widescreen_projection(const NfsuWidescreenLayout *layout,
                                int front_end, float *x, float *y)
{
    if (layout->mode == NFSU_WIDESCREEN_ADAPTIVE) {
        /* Garage and race both keep the current vertical FOV. Widening
         * the display adds horizontal view instead of enlarging the car.
         * Deriving X from Y also removes the guest's approximate FOV ratio. */
        if (isfinite(*x) && isfinite(*y) && *x != 0 && *y != 0)
            *x = copysignf(fabsf(*y) / layout->aspect, *x);
    } else if (layout->mode == NFSU_WIDESCREEN_LEGACY) {
        if (front_end) *y *= 4.0f / 3.0f;
        else *x *= 0.75f;
    }
}

float nfsu_widescreen_anchor(const NfsuWidescreenLayout *layout, float centre)
{
    if (centre < 280.0f) return -layout->edge_shift;
    if (centre > 360.0f) return layout->edge_shift;
    return 0;
}

float nfsu_widescreen_garage_yaw(const NfsuWidescreenLayout *layout)
{
    if (layout->mode == NFSU_WIDESCREEN_NATIVE) return 0;
    const char *value = getenv("RECOMP_FE_YAW");
    return (value ? (float)atof(value) : 5.0f) * 3.14159265f / 180.0f;
}
