#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "matrix_native.h"

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

int nfsu_matrix_native(void)
{
#if defined(__ANDROID__) && defined(__aarch64__)
    /* Live A/B switch, sampled outside the arithmetic. TLS also supports the
     * optional native-thread guest mode without races between callers. */
    static RECOMP_TLS unsigned calls;
    static RECOMP_TLS int enabled = 1;
    if (!(calls++ & 65535u)) {
        char value[PROP_VALUE_MAX];
        __system_property_get("debug.nfsu.native_matrix", value);
        enabled = !(value[0] == '0' && !value[1]);
    }
    return enabled && matrix_try_native();
#else
    return 0;
#endif
}
