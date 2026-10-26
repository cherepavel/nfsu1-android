/* Per-thread aggregate timings. No registry, shared counter, or collector mutex. */
#include "../xboxrecomp/include/nfsu_log.h"
#include "../xboxrecomp/include/nfsu_perf.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

int nfsu_perf_enabled;
typedef struct { uint64_t count, sum, max; } PerfMetric;
#define PERF_KERNEL_SITES 128
typedef struct {
    uint32_t ordinal, caller;
    uint64_t count, kernel_ns, wait_ns, wait_max;
} PerfKernel;
typedef struct {
    uint64_t window, cpu;
    const char *role;
    PerfMetric metrics[NFSU_PERF_COUNT];
    PerfKernel sites[PERF_KERNEL_SITES], overflow, resumes;
} PerfThread;
static _Thread_local PerfThread perf;
/* Separate from five-second windows; only non-overlapping wait/API metrics. */
static _Thread_local struct {
    uint64_t last, cpu, report_window;
    unsigned reports;
    PerfMetric metrics[NFSU_PERF_COUNT];
} frame_perf;

void nfsu_perf_role(const char *role) { perf.role = role; }

static uint64_t perf_clock(clockid_t clock)
{
    struct timespec t;
    if (clock_gettime(clock, &t) != 0) return 0;
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

uint64_t nfsu_perf_begin(void)
{
    if (!nfsu_perf_enabled) return 0;
    uint64_t now = perf_clock(CLOCK_MONOTONIC);
    if (!perf.window) {
        perf.window = now;
        perf.cpu = perf_clock(CLOCK_THREAD_CPUTIME_ID);
    }
    return now;
}

void nfsu_perf_end(int metric, uint64_t start)
{
    if (!start || metric < 0 || metric >= NFSU_PERF_COUNT) return;
    uint64_t now = perf_clock(CLOCK_MONOTONIC);
    uint64_t elapsed = now >= start ? now - start : 0;
    nfsu_perf_add(metric, elapsed);
}

void nfsu_perf_add(int metric, uint64_t elapsed)
{
    if (!nfsu_perf_enabled || metric < 0 || metric >= NFSU_PERF_COUNT) return;
    PerfMetric *m = &perf.metrics[metric];
    m->count++;
    m->sum += elapsed;
    if (elapsed > m->max) m->max = elapsed;
    if ((metric >= NFSU_PERF_VK_FENCE && metric <= NFSU_PERF_VK_PRESENT)
        || metric >= NFSU_PERF_PB_IDLE_TIMEOUT) {
        m = &frame_perf.metrics[metric];
        m->count++;
        m->sum += elapsed;
        if (elapsed > m->max) m->max = elapsed;
    }
}

uint64_t nfsu_perf_gil_wait_sum(void)
{
    return perf.metrics[NFSU_PERF_GIL_WAIT].sum;
}

static void kernel_add(PerfKernel *site, uint64_t kernel_ns, uint64_t wait_ns)
{
    site->count++;
    site->kernel_ns += kernel_ns;
    site->wait_ns += wait_ns;
    if (wait_ns > site->wait_max) site->wait_max = wait_ns;
}

void nfsu_perf_kernel_resume(uint32_t ordinal, uint32_t caller,
                            uint64_t kernel_ns, uint64_t wait_before)
{
    if (!nfsu_perf_enabled) return;
    uint64_t after = nfsu_perf_gil_wait_sum();
    uint64_t wait_ns = after >= wait_before ? after - wait_before : 0;
    kernel_add(&perf.resumes, kernel_ns, wait_ns);
    unsigned hash = (ordinal * 0x9e3779b1u) ^ (caller * 0x85ebca6bu);
    for (unsigned i = 0; i < PERF_KERNEL_SITES; i++) {
        PerfKernel *site = &perf.sites[(hash + i) & (PERF_KERNEL_SITES - 1)];
        if (!site->count || (site->ordinal == ordinal && site->caller == caller)) {
            site->ordinal = ordinal;
            site->caller = caller;
            kernel_add(site, kernel_ns, wait_ns);
            return;
        }
    }
    kernel_add(&perf.overflow, kernel_ns, wait_ns);
}

void nfsu_perf_poll(void)
{
    static const char *names[NFSU_PERF_COUNT] = {
        "gil_wait", "gil_held", "apu_wait", "apu_work", "vk_draw", "vk_flip",
        "vk_fence", "vk_acquire", "vk_submit", "vk_present", "pb_work",
        "gil_cs", "gil_queue", "gil_handoff",
        "spin_wait", "spin_total", "preempt_wait", "preempt_total",
        "tex_hash", "tex_image", "tex_staging", "tex_decode", "tex_dxt",
        "tex_upload", "tex_mips", "tex_snapshot", "tex_worker_wait",
        "tex_worker_cpu", "tex_worker_copy",
        "pb_idle_timeout", "pb_idle_wake", "pb_idle_error", "pb_semaphore"
    };
    if (!nfsu_perf_enabled || !perf.window) return;
    uint64_t now = perf_clock(CLOCK_MONOTONIC);
    if (now - perf.window < 5000000000ull) return;
    uint64_t cpu = perf_clock(CLOCK_THREAD_CPUTIME_ID);
    char line[2048];
    int used = snprintf(line, sizeof(line),
        "[PERF5] tid=%ld role=%s window_ms=%.1f cpu_ms=%.1f"
        " resume_calls=%llu resume_wait_ms=%.2f resume_kernel_ms=%.2f",
        (long)syscall(SYS_gettid), perf.role ? perf.role : "worker", (now - perf.window) / 1e6,
        cpu >= perf.cpu ? (cpu - perf.cpu) / 1e6 : 0.0,
        (unsigned long long)perf.resumes.count,
        perf.resumes.wait_ns / 1e6, perf.resumes.kernel_ns / 1e6);
    for (int i = 0; i < NFSU_PERF_COUNT; i++) {
        PerfMetric *m = &perf.metrics[i];
        if (!m->count) continue;
        if (used < 0 || (size_t)used >= sizeof(line)) break;
        int n = snprintf(line + used, sizeof(line) - (size_t)used,
            " %s=%llu/%.2f/%.2f", names[i],
            (unsigned long long)m->count, m->sum / 1e6, m->max / 1e6);
        if (n < 0) break;
        used += n;
    }
    /* Bounded output: one summary and at most four sites plus overflow. */
    NFS_LOG_INFO("performance", "%s\n", line);
    int selected[4] = {-1, -1, -1, -1};
    for (int rank = 0; rank < 4; rank++) {
        int best = -1;
        for (int i = 0; i < PERF_KERNEL_SITES; i++) {
            int seen = 0;
            for (int j = 0; j < rank; j++) if (selected[j] == i) seen = 1;
            if (seen || !perf.sites[i].count) continue;
            if (best < 0 || perf.sites[i].wait_ns > perf.sites[best].wait_ns) best = i;
        }
        if (best < 0) break;
        selected[rank] = best;
        PerfKernel *site = &perf.sites[best];
        NFS_LOG_INFO("performance", "[PERFK] tid=%ld role=%s window_ms=%.1f rank=%d"
            " ordinal=%u caller=%08X calls=%llu kernel_ms=%.2f wait_ms=%.2f wait_max_ms=%.2f\n",
            (long)syscall(SYS_gettid), perf.role ? perf.role : "worker",
            (now - perf.window) / 1e6, rank + 1, site->ordinal, site->caller,
            (unsigned long long)site->count, site->kernel_ns / 1e6,
            site->wait_ns / 1e6, site->wait_max / 1e6);
    }
    if (perf.overflow.count)
        NFS_LOG_INFO("performance", "[PERFK] tid=%ld bucket=overflow calls=%llu wait_ms=%.2f\n",
            (long)syscall(SYS_gettid), (unsigned long long)perf.overflow.count,
            perf.overflow.wait_ns / 1e6);
    memset(perf.metrics, 0, sizeof(perf.metrics));
    memset(perf.sites, 0, sizeof(perf.sites));
    memset(&perf.overflow, 0, sizeof(perf.overflow));
    memset(&perf.resumes, 0, sizeof(perf.resumes));
    perf.window = now;
    perf.cpu = cpu;
}

/* Called at the same flip-entry boundary as hitch/texstage. The prior flip's
 * submit/present/fence costs belong to this callback-to-callback interval. */
void nfsu_perf_frame(uint32_t frame, uint64_t now)
{
    if (!nfsu_perf_enabled) return;
    uint64_t cpu = perf_clock(CLOCK_THREAD_CPUTIME_ID);
    if (now - frame_perf.report_window > 10000000000ull) {
        frame_perf.report_window = now;
        frame_perf.reports = 0;
    }
    if (frame_perf.last && now > frame_perf.last
        && now - frame_perf.last > 50000000ull && frame_perf.reports++ < 30) {
        char line[1536];
        int used = snprintf(line, sizeof line,
            "[FRAMEWAIT] frame=%u begin_ns=%llu end_ns=%llu wall_ms=%.3f cpu_ms=%.3f",
            frame, (unsigned long long)frame_perf.last, (unsigned long long)now,
            (now - frame_perf.last) / 1e6,
            (cpu >= frame_perf.cpu ? cpu - frame_perf.cpu : 0) / 1e6);
        static const int ids[] = {NFSU_PERF_PB_IDLE_TIMEOUT, NFSU_PERF_PB_IDLE_WAKE, NFSU_PERF_PB_IDLE_ERROR,
            NFSU_PERF_PB_SEMAPHORE, NFSU_PERF_VK_FENCE, NFSU_PERF_VK_ACQUIRE,
            NFSU_PERF_VK_SUBMIT, NFSU_PERF_VK_PRESENT};
        static const char *names[] = {"idle_timeout", "idle_wake", "idle_error", "pb_semaphore",
            "vk_fence", "vk_acquire", "vk_submit", "vk_present"};
        for (unsigned i = 0; i < sizeof ids / sizeof ids[0] && used > 0
             && (size_t)used < sizeof line; i++) {
            PerfMetric *m = &frame_perf.metrics[ids[i]];
            used += snprintf(line + used, sizeof line - (size_t)used,
                " %s=%llu/%.3f/%.3f", names[i], (unsigned long long)m->count,
                m->sum / 1e6, m->max / 1e6);
        }
        NFS_LOG_INFO("performance", "%s\n", line);
    }
    frame_perf.last = now;
    frame_perf.cpu = cpu;
    memset(frame_perf.metrics, 0, sizeof frame_perf.metrics);
}
