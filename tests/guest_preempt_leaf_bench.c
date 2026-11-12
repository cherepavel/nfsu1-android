/* Build the probe as a PIC shared library with the real guest_fiber.c.
 * Load before/after separately to retain dynamic-library call/TLS behavior.
 * This measures query cost, not scheduler throughput or game FPS. */
#ifdef NFSU_PREEMPT_LOADER
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    assert(argc == 3);
    for (unsigned scenario = 0; scenario < 3; scenario++) {
        for (unsigned pair = 0; pair < 3; pair++) for (unsigned order = 0; order < 2; order++) {
            unsigned mode = (pair + order) % 2;
            void *library = dlopen(argv[1 + mode], RTLD_NOW | RTLD_LOCAL);
            if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
            int (*probe)(unsigned) = (int (*)(unsigned))dlsym(library, "preempt_leaf_benchmark");
            assert(probe);
            printf("mode=%s pair=%u ", mode ? "candidate" : "reference", pair);
            assert(!probe(scenario));
            assert(!dlclose(library));
        }
    }
}
#else
#include "kernel/guest_fiber.h"
#include "kernel/recomp_context.h"
#include "recomp_context_registers.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
static void probe_entry(void *argument)
{
    unsigned scenario = *(unsigned *)argument;
    unsigned count = scenario == 1 ? 5000000 : 20000000;
    RecompKernelState *kernel = &recomp_context_current()->kernel;
    XgfExecutor *executor = g_recomp_registers.native_executor;
    assert(executor); kernel->gil_depth = 1;
    (void)xgf_preempt_status_for_executor(executor); /* Warm the ready cache. */
    struct timespec start, end;
    assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start));
    unsigned sum = 0;
    for (unsigned i = 0; i < count; i++) sum += xgf_preempt_status_for_executor(executor);
    assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end));
    if (!scenario) assert(sum == count * XGF_PREEMPT_STAY);
    else if (scenario == 2) assert(sum == count * XGF_PREEMPT_YIELD);
    else assert(sum >= count * XGF_PREEMPT_STAY && sum <= count * XGF_PREEMPT_YIELD);
    kernel->gil_depth = 0;
    double ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf("scenario=%s checks=%u cpu_ms=%.3f ns_per_check=%.3f checksum=%u\n",
        scenario == 0 ? "quiet" : scenario == 1 ? "equal_clock" : "higher", count, ns / 1e6, ns / count, sum);
}
int preempt_leaf_benchmark(unsigned scenario)
{
    assert(scenario < 3);
    XgfExecutor *executor = xgf_executor_create(); assert(executor);
    XgfFiber *self = xgf_create(executor, probe_entry, &scenario, 1048576, 0x4000); assert(self);
    XgfFiber *peer = NULL;
    if (scenario) {
        peer = xgf_create(executor, probe_entry, &scenario, 1048576, 0x5000); assert(peer);
        assert(xgf_set_priority(peer, scenario == 2 ? 1 : 0));
    }
    assert(xgf_resume(executor, self) == XGF_EXITED);
    assert(xgf_destroy(self)); if (peer) assert(xgf_destroy(peer));
    assert(xgf_executor_destroy(executor)); return 0;
}
#endif
