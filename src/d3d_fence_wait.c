#include <stdint.h>
#ifdef __ANDROID__
#include <pthread.h>
#include <sys/system_properties.h>
#include "nfsu_log.h"
#include "recomp_spin_wait.h"

extern int nv2a_pb_wait_semaphore(uint32_t va, uint32_t submitted, uint32_t delta);

static int fence_poll_wait(const RecompSpinWait *poll, int block)
{
    /* Only the final semaphore loop of this title's D3D_BlockOnTime.
     * ax = submitted - requested; dx = semaphore VA; si = submitted.
     * Keeping the lifted loop preserves its exact completion predicate,
     * register effects, kickoff/callback paths, and ret 8 convention. */
    if (poll->pc != 0x001D0470u) return 0;
    static _Thread_local unsigned calls;
    static _Thread_local int enabled = 1, reported = -1;
    if (!block && !(calls++ & 127u)) {
        char value[PROP_VALUE_MAX];
        __system_property_get("debug.nfsu.fence_wait", value);
        enabled = !(value[0] == '0' && !value[1]);
        if (reported != enabled) {
            NFS_LOG_INFO("performance", "[FENCEWAIT] native event wait=%d\n", enabled);
            reported = enabled;
        }
    }
    if (!enabled) return 0;
    return block ? nv2a_pb_wait_semaphore(poll->dx, poll->si, poll->ax) : 1;
}

static void register_fence_wait(void) { xbox_set_spin_wait_hook(fence_poll_wait); }
#endif

void nfsu_d3d_fence_wait_init(void)
{
#ifdef __ANDROID__
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, register_fence_wait);
#endif
}
