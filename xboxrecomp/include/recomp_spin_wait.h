#ifndef RECOMP_SPIN_WAIT_H
#define RECOMP_SPIN_WAIT_H
#include <stdint.h>

/* A title may recognize an exact lifted polling loop. With block=0 the
 * callback only checks whether it can handle this poll, under guest ownership.
 * With block=1 it waits after releasing guest ownership. Other loops retain
 * their existing scheduler/host-yield behavior. Register values
 * are snapshots from the lifted locals, not stale global register mirrors. */
typedef struct {
    uint32_t pc, ax, cx, dx, si;
} RecompSpinWait;
typedef int (*RecompSpinWaitHook)(const RecompSpinWait *poll, int block);
void xbox_set_spin_wait_hook(RecompSpinWaitHook hook);
#endif
