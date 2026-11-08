/* Query-only host regression: compile the real implementation, but never swap
 * native stacks. Full stack/lifecycle coverage remains in guest_executor.c. */
#include <pthread.h>
#include <stdlib.h>
#ifdef __APPLE__
/* Executor creation is not exercised here. Darwin lacks this Linux API. */
static int unused_condattr_clock(pthread_condattr_t *attr, clockid_t clock)
{ (void)attr; (void)clock; abort(); }
#define pthread_condattr_setclock unused_condattr_clock
#endif
#include "../xboxrecomp/src/kernel/guest_fiber.c"
#include "recomp_context_registers.h"
#include <assert.h>
#include <stdio.h>

/* Only last-error storage is needed from the host facade in this query-only
 * test. Unused stack/lifecycle functions are removed by linker dead stripping. */
static RECOMP_TLS DWORD last_error;
DWORD GetLastError(void) { return last_error; }
void SetLastError(DWORD error) { last_error = error; }

void xgf_native_swap(NativeContext *previous, const NativeContext *next)
{ (void)previous; (void)next; abort(); }
void xgf_native_bootstrap(void) { abort(); }

static void check(XgfPreemptStatus expected)
{
    assert(xgf_preempt_status() == expected);
    assert(xgf_preempt_status_for_executor(g_recomp_registers.native_executor) == expected);
    assert(xgf_current_executor() == g_recomp_registers.native_executor);
}
static void *foreign(void *unused)
{
    (void)unused;
    assert(!g_recomp_registers.native_executor);
    check(XGF_PREEMPT_NATIVE);
    return NULL;
}

int main(void)
{
    check(XGF_PREEMPT_NATIVE);
    XgfExecutor e = {.owner = owner_id()};
    XgfFiber self = {.executor = &e, .preempt_valid = 1};
    XgfFiber peer = {.executor = &e, .state = XGF_NEW, .priority = 1};
    assert(!pthread_mutex_init(&e.wake_lock, NULL));
    assert(!pthread_cond_init(&e.wake, NULL));
    e.active = &self;
    running = &e;
    self.context.kernel.gil_depth = 1;
    check(XGF_PREEMPT_STAY);
    self.suspended = 1; check(XGF_PREEMPT_YIELD); self.suspended = 0;
    e.list = &peer; self.preempt_valid = 0;
    check(XGF_PREEMPT_YIELD);
    int *guards[] = {&e.on_native_callback, &self.context.kernel.gil_nopreempt,
                    &self.context.kernel.dispatch_held};
    for (unsigned i = 0; i < sizeof(guards) / sizeof(guards[0]); i++) {
        *guards[i] = 1; check(XGF_PREEMPT_STAY); *guards[i] = 0;
    }
    self.context.kernel.gil_depth = 0; check(XGF_PREEMPT_STAY);
    self.context.kernel.gil_depth = 1;
    assert(xgf_set_priority(&peer, 0));
    self.slice_start = monotonic_ms() + 1000000; check(XGF_PREEMPT_STAY);
    self.slice_start = 0; check(XGF_PREEMPT_YIELD);
    assert(xgf_set_priority(&peer, -1)); check(XGF_PREEMPT_STAY);
    assert(xgf_set_priority(&peer, 1));
    peer.state = XGF_WAITING; self.preempt_valid = 0; check(XGF_PREEMPT_STAY);
    xgf_notify(&peer); check(XGF_PREEMPT_YIELD);

    pthread_t thread;
    assert(!pthread_create(&thread, NULL, foreign, NULL));
    assert(!pthread_join(thread, NULL));
    assert(g_recomp_registers.native_executor == &e);
    /* Native ownership is not part of the architectural context snapshot. */
    RecompContext context;
    RecompContext *original = recomp_context_current();
    assert(recomp_context_init(&context, 0x9000));
    assert(recomp_context_switch(&context));
    assert(g_recomp_registers.native_executor == &e);
    assert(recomp_context_switch(original));
    assert(g_recomp_registers.native_executor == &e);
    e.active = NULL; check(XGF_PREEMPT_NATIVE);
    running = NULL; check(XGF_PREEMPT_NATIVE);
    pthread_cond_destroy(&e.wake);
    pthread_mutex_destroy(&e.wake_lock);
    puts("PASS: executor-argument parity, cached priorities/quantum/wake, guards, native TLS isolation and context ownership");
}
