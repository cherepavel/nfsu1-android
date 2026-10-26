# Renderer scheduler trace, Poco, 26 October 2026

A first capture at about 20:48:57 was interrupted by game SIGSEGV after
2.4 seconds; no hitch interval was captured. A subsequent 30-second retry
survived, but noisy tracing overwrote buffer data and did not cover logged
hitches. These captures are excluded from hitch conclusions.

The accepted 45-second capture uses sched_switch/waking/wakeup, blocked_reason,
CPU frequency/idle, sync atrace, process stats and frame timeline, with a
64 MiB buffer flushed to file every second. PID 32351, renderer TID 9303,
executor TID 9298. Scheduler data spans 44.924 seconds, around
20:53:30–20:54:15 MSK. Settings and APK were not changed during capture.

REALTIME clock snapshots map log wall timestamps to Perfetto BOOTTIME.
As before, log timestamps and rounded hitch durations introduce approximately
1 ms boundary uncertainty, plus potential logging delay. These are callback
intervals, not display presentation times.

| Frame | Wall ms | Renderer Running | Sleeping S | Runnable R/R+ | tex_get wall |
|---|---:|---:|---:|---:|---:|
| 7747 | 66 | 21.22 ms | 41.66 ms | 3.11 ms | 1 ms |
| 9189 | 50 | 28.00 ms | 18.32 ms | 3.68 ms | 13 ms |
| 9733 | 54 | 46.21 ms | 3.06 ms | 4.73 ms | 27 ms |

The texture-light 66 ms frame is primarily a sleeping/waiting interval, not
renderer CPU execution or scheduling starvation. Sleep consists of repeated
roughly 1.0–1.05 ms intervals. Executor in the same interval runs 34.22 ms,
sleeps 27.43 ms and has 2.88 ms uninterruptible sleep. This suggests tracing
command production/waits rather than assuming a driver stall.

`nv2a_thread_pause(0)` in xbox_memory_layout.c uses pthread_cond_timedwait
with a 1 ms deadline when pushbuffer is not moving and GET=PUT. Its pattern
is compatible with the observed sleeps. This is a hypothesis, not a stack
attribution: blocked_function and waker identity are unavailable in this
trace. Neither a GPU fence nor a pushbuffer wait is proven by state S alone.

Across the whole trace renderer Running=41997 ms, Sleeping=2212 ms,
Runnable=710 ms, uninterruptible sleep about 1.5 ms. Most frames are CPU busy,
but the texture-light hitch differs. Renderer runs almost entirely on CPUs
4–7. Frequency tracks for those CPUs range from 400 MHz to 1.6 GHz; this
alone neither establishes thermal throttling nor explains the isolated hitch.

No positive ftrace overrun/lost or buffer overwrite counters in the accepted
capture; two traced_chunks_discarded are reported, so the trace is not claimed
perfectly lossless. Some requested ftrace events were unavailable. Missing
wake/block attribution must not be silently replaced by inferred call stacks.

Next precise measurement: timed scopes/counters for nv2a_thread_pause,
pushbuffer/fence waits, and vk submit/present/fence, with monotonic frame bounds.
This will distinguish repeated idle polls from driver waits and correlate
producer progress. Do not remove the 1 ms wait or busy-spin based solely on
this trace: it may simply reflect the producer having no commands ready.

Raw traces, SQLite exports and scripts are local ignored files:
performance-results/2026-10-26/scheduler{,-retry,-clean}/.

## Direct FRAMEWAIT verification, 20:59:53–21:00:10 MSK

New diagnostic APK, PID 9370, renderer TID 13824. The user opened a race.
The per-frame scopes now directly establish that some texture-light hitches
are spent in `nv2a_thread_pause` on the idle pushbuffer path:

| Frame | Wall ms | Renderer CPU ms | Idle timeout+wake ms | Vulkan fence+acquire+submit+present ms | Texture ms |
|---|---:|---:|---:|---:|---:|
| 1922 | 67.566 | 34.376 | 32.379 | 1.795 | 3 |
| 2156 | 68.758 | 19.608 | 30.272 | 3.309 | 0 |
| 2617 | 74.331 | 21.321 | 53.048 | 2.030 | 1 |

Frame 2617 has 42 timeout-ended idle waits totaling 51.948 ms, plus three
wake-ended waits totaling 1.100 ms. Semaphore wait is zero. This attributes
its main delay to the application idle pushbuffer wait, not Vulkan fence or
present. `idle_timeout` is complete wall scope (including scheduling delay
and mutex work), not exactly time asleep. The sum of scopes and CPU is not
an independent additive partition: scope CPU is already part of CPU_ms.

Not all hitches share that cause: frame 2218 is 64.635 ms wall / 52.406 ms CPU,
43 ms texture work and only 0.867 ms idle wake. Frame 2156 also leaves a
substantial residual outside measured waits and CPU, requiring scheduler
correlation rather than forcing it into a measured category.

Next: capture executor progress/stacks and PUT/GET transitions over an idle-heavy
frame. Repeated timeout does not itself prove the 1 ms policy is the root cause:
the executor may have no completed commands to submit during that interval.
Changing sleep to spin before identifying the producer delay can increase CPU
without reducing the stall. Current data narrows the relevant boundary to
producer → pushbuffer publication → renderer wake/consumption.

Raw FRAMEWAIT/PERF5 and matching graphics lines are local under
performance-results/2026-10-26/framewait-race/.

## Source audit of publication/wake path

The generic fallback of `xbox_mmio_write` only stores the register value;
there is no DMA_PUT-specific notification. `recomp_spin_wake` is invoked by
RECOMP_SPIN_HINT every 16 polling iterations. The renderer otherwise polls
through `nv2a_thread_pause` with a 1 ms condvar timeout. This means wakeups
are tied to guest polling rather than directly to command publication.
Registered device handlers and uninstrumented/direct guest writes must be
checked before assuming the generic MMIO path covers every DMA_PUT write.

A concrete missed-notification interleaving is possible:

1. Ack thread reads PUT==GET and chooses busy=false at the loop tail.
2. Producer publishes a new PUT and invokes wake while no hardware thread
   is registered as a sleeper; the fast path returns without a generation bump.
   Alternatively a generation bump occurs before the ack thread captures seen.
3. Ack thread enters pause, copies the current generation and waits without
   rechecking PUT/GET. It can sleep until timeout despite work being pending.

There is no permanent deadlock because of the 1 ms timeout. One such incident
adds at most the timeout plus scheduling/lock delays; many incidents can add
across many small submissions, but the measured 42 timeouts are not proof that
42 missed notifications occurred. Most could be correct empty-queue waits.

A safe candidate change is predicate recheck after enrolling the sleeper under
the shared lock, plus notification at verified command publication sites.
Both ends must share a sound ordering protocol. Simply removing the timeout,
busy-spinning or broadcasting more often is not a demonstrated solution.
Before attributing the entire hitch to this code, record publication and
consumption timestamps/PUT values to separate ready-but-sleeping latency from
late production. No source behavior was changed by this audit.
