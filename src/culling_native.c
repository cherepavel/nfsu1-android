#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "culling_native.h"
#include "../xboxrecomp/include/nfsu_log.h"
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif
int nfsu_culling_native(void)
{
#if defined(__ANDROID__) && defined(__aarch64__)
    static RECOMP_TLS unsigned calls;
    static RECOMP_TLS int enabled;
    if (!(calls++ & 65535u)) {
        char value[PROP_VALUE_MAX]={0};
        __system_property_get("debug.nfsu.native_culling",value);
        enabled=value[0]=='1' && !value[1];
    }
    if (!enabled) return 0;
    RECOMP_PREEMPT();
    int hit = culling_try_native();
    static RECOMP_TLS unsigned hits, fallback;
    if (hit) hits++; else fallback++;
    if (!((hits + fallback) & 262143u)) {
        NFS_LOG_INFO("performance", "[CULLNATIVE] hits=%u fallback=%u (since report)\n", hits, fallback);
        hits = fallback = 0;
    }
    return hit;
#else
    return 0;
#endif
}
