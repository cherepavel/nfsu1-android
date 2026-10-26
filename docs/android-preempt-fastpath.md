# Cached preemption fast path, Android ARM64

26 October 2026. Fresh executor race profile: xgf_preempt_status is 7.42% self,
fiber_should_preempt 3.57% self; dynamic TLS resolution is 16.09% self across
all executor callers. Inclusive percentages overlap and are not additive.
Diagnostics and safe points are retained.

## Change

fiber_should_preempt previously contained both the cached decision and the
mutex/list refresh. ARM64 code kept it out of line, so every safe point went
through xgf_preempt_status -> fiber_should_preempt even when the generation
was unchanged. The refresh is now a cold, noinline helper; the small decision
is always_inline. The generated ARM64 fast path has no call to the old helper.
Only stale/invalid caches call fiber_refresh_preempt.

The suspended load and acquire generation load, cached higher/equal decisions,
equal-priority slice clock test, callback/dispatch/GIL guards, generation
snapshot timing, and list classification logic are unchanged. No safe point,
notification or clock check is skipped relative to the old logic. Static TLS
models, global owner pointers and register ABI were not changed; TLS lookup
cost remains and must not be claimed eliminated.

## Checks

Existing Android ARM64 guest_fiber regression passes (2000 yields, stack,
GP/FP/kernel context, longjmp, native ownership/lifecycle). guest_executor
regression passes (priorities, cached invalidation on priority/membership and
native notification, non-preemptible/dispatch guards, suspend/resume, deadlines,
1000 messages and handle lifetime).

A benchmark compiles the actual old and new guest_fiber.c into separate
-fPIC shared libraries at O2, loaded with dlopen on the phone. This preserves
dynamic TLS access unlike benchmarking only an executable with relaxed TLS.
It calls xgf_preempt_status 20 million times in an active guest fiber with
GIL depth 1, no competitors and a stable generation. Alternating three pairs,
CLOCK_THREAD_CPUTIME_ID:

| Pair | Before CPU ms | After CPU ms |
|---|---:|---:|
| 1 | 389.985 | 315.804 |
| 2 | 391.015 | 316.587 |
| 3 | 360.988 | 288.265 |

All checksums 20000000 (STAY). About 19–20% cheaper for this cached path.
This does not measure refresh-heavy or equal-priority-clock performance,
whole-game speed, power or heating. CPU clocks were not locked; regression
covers semantics for more paths but is not an efficiency benchmark for them.

assembleDebug/lintDebug pass. APK SHA256:
8fe07fba926a0885fb3fcee3d03e2217f14074b7e869e31ae109fb7f2ee5918f.
Not installed by this change. The earlier native-culling candidate remains
opt-in/off; phone property was already restored to 0. Keep it off for the next
race comparison. Logging/diagnostics stay enabled.

Raw sources, binaries' assembly, benchmark output and regression/build logs
are saved locally under performance-results/2026-10-26/preempt-fastpath/.

## Live race check

User installed the matching APK (verified installed base.apk SHA256 above).
Native culling property remains 0. A 25-second executor recording on thread
26579 collected 24,836 samples with zero losses; race screen verified.
Four complete roughly 5-second PERF5 windows ending in 22:40:23–22:40:42
show executor 96.39% of a core, renderer 86.96%, APU 31.24%, 54.73 flips/s.

Compared with the previous culling-disabled race recording, recomp_preempt
inclusive share is 17.81% versus 16.62%; all dynamic TLS resolver self time
16.55% versus 16.17%. xgf_preempt_status self time now includes the inlined
cached decision and cannot be compared alone with its old self time.
The cold refresh is only 0.13% inclusive. Scenes/workloads differ (including
culling subtree 7.79% versus 5.34%); this is neither a controlled regression
measurement nor evidence of whole-game improvement. No heat/power reduction
is demonstrated. Keep the microbenchmark result separate from game results.
Raw capture/report and counter summary: preempt-fastpath/race/.
