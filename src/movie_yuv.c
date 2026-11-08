#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "movie_yuv_native.h"
#ifdef __ANDROID__
#include <sys/system_properties.h>
#include "nfsu_log.h"
#endif

/* A separate, optimized translation unit keeps Debug APKs from expanding
 * each NEON intrinsic into an unoptimized wrapper. */
int nfsu_movie_yuv_native(void)
{
#if defined(__aarch64__)
#ifdef __ANDROID__
    static unsigned calls;
    static int enabled = 1, reported = -1;
    if (!(calls++ & 4095u)) {
        char value[PROP_VALUE_MAX];
        __system_property_get("debug.nfsu.native_yuv", value);
        enabled = !(value[0] == '0' && !value[1]);
        if (reported != enabled) {
            NFS_LOG_INFO("performance", "[MOVIEYUV] native NEON=%d\n", enabled);
            reported = enabled;
        }
    }
    if (!enabled) return 0;
#endif
    return movie_yuv_try_native();
#else
    return 0;
#endif
}
