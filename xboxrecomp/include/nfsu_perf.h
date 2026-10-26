#ifndef NFSU_PERF_H
#define NFSU_PERF_H

#include <stdint.h>

enum {
    NFSU_PERF_GIL_WAIT, NFSU_PERF_GIL_HELD, NFSU_PERF_APU_WAIT,
    NFSU_PERF_APU_WORK, NFSU_PERF_VK_DRAW, NFSU_PERF_VK_FLIP,
    NFSU_PERF_VK_FENCE, NFSU_PERF_VK_ACQUIRE, NFSU_PERF_VK_SUBMIT,
    NFSU_PERF_VK_PRESENT, NFSU_PERF_PB_WORK,
    NFSU_PERF_GIL_CS, NFSU_PERF_GIL_QUEUE, NFSU_PERF_GIL_HANDOFF,
    NFSU_PERF_SPIN_WAIT, NFSU_PERF_SPIN_TOTAL,
    NFSU_PERF_PREEMPT_WAIT, NFSU_PERF_PREEMPT_TOTAL,
    NFSU_PERF_TEX_HASH, NFSU_PERF_TEX_IMAGE, NFSU_PERF_TEX_STAGING,
    NFSU_PERF_TEX_DECODE, NFSU_PERF_TEX_DXT, NFSU_PERF_TEX_UPLOAD,
    NFSU_PERF_TEX_MIPS, NFSU_PERF_TEX_SNAPSHOT, NFSU_PERF_TEX_WORKER_WAIT,
    NFSU_PERF_TEX_WORKER_CPU, NFSU_PERF_TEX_WORKER_COPY,
    NFSU_PERF_PB_IDLE_TIMEOUT, NFSU_PERF_PB_IDLE_WAKE, NFSU_PERF_PB_IDLE_ERROR, NFSU_PERF_PB_SEMAPHORE,
    NFSU_PERF_COUNT
};

#if defined(__ANDROID__) || defined(NFSU_PERF_HOST_TEST)
/* Immutable after boot. Each producer owns its counters in TLS. */
extern int nfsu_perf_enabled;
uint64_t nfsu_perf_begin(void);
void nfsu_perf_end(int metric, uint64_t start);
void nfsu_perf_add(int metric, uint64_t elapsed);
void nfsu_perf_poll(void);
void nfsu_perf_frame(uint32_t frame, uint64_t now);
void nfsu_perf_role(const char *role);
uint64_t nfsu_perf_gil_wait_sum(void);
void nfsu_perf_kernel_resume(uint32_t ordinal, uint32_t caller,
                            uint64_t kernel_ns, uint64_t wait_before);
#else
static inline uint64_t nfsu_perf_begin(void) { return 0; }
static inline void nfsu_perf_end(int metric, uint64_t start)
{ (void)metric; (void)start; }
static inline void nfsu_perf_add(int metric, uint64_t elapsed)
{ (void)metric; (void)elapsed; }
static inline void nfsu_perf_poll(void) { }
static inline void nfsu_perf_frame(uint32_t frame, uint64_t now)
{ (void)frame; (void)now; }
static inline void nfsu_perf_role(const char *role) { (void)role; }
static inline uint64_t nfsu_perf_gil_wait_sum(void) { return 0; }
static inline void nfsu_perf_kernel_resume(uint32_t ordinal, uint32_t caller,
                                         uint64_t kernel_ns, uint64_t wait_before)
{ (void)ordinal; (void)caller; (void)kernel_ns; (void)wait_before; }
#endif

#endif
