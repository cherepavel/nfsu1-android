# Executor argument at generated safe points

This optional experiment follows the grouped-register measurements in
[android-register-block.md](android-register-block.md). In the repeated ASUS
race profile, resolver samples attributed to `xgf_preempt_status` were 5.39%
of executor samples and 59.95% of remaining dynamic resolver samples.
Those percentages identify a target; they are not a predicted FPS gain.

## Implementation

Build option `NFSU2_EXECUTOR_TLS_ARG` defaults to OFF and requires Android
and `NFSU2_REGISTER_BLOCK`. The Gradle property is `nfsu.executorTlsArg`.
The normal `g_gil_contended` load and its relaxed ordering are unchanged.
When it requests a safe point, the generated caller passes the native executor
pointer stored in `g_recomp_registers.native_executor` to
`recomp_preempt_for_executor`. That bridge uses
`xgf_preempt_status_for_executor` rather than performing another TLS lookup.
Generated register accesses can reuse the caller's TLS block address.

In this mode the block's native executor slot replaces `guest_fiber.c`'s
separate `running` TLS variable; it is the sole source of truth, not a second
cache to invalidate. Existing resume/return sites publish and clear it.
Native-stack callbacks retain the slot and the existing callback guard.
The slot is native-owned and is deliberately excluded from architectural
`RecompCpuState` save/load. Native-backed guest threads see a NULL slot and
use the unchanged native scheduler fallback.

The explicit-pointer query is internal: callers must supply their current
native TLS slot, not an executor from another thread or a cached lifecycle.
Both query entries inline the same status implementation. GIL/no-preempt,
dispatch/callback guards, suspension, generation acquire loads, higher-priority
readiness and equal-priority slice clock checks are retained. Safe points are
not skipped. Logging and native culling are unaffected.

## Checks performed before APK build

The real query implementation passed a host regression covering old/new
query parity, higher/lower/equal priorities, quiet cache, slice expiry,
notification invalidation, suspension, GIL/callback/dispatch guards, native
TLS isolation and the native slot surviving architectural context switches.
It also passed AddressSanitizer and UndefinedBehaviorSanitizer. This host
test intentionally does not switch C stacks or exercise full fiber lifecycle.

The compiled generator test passed in separate-register, grouped-register and
grouped-plus-argument configurations. It checks the contended and uncontended
safe-point macro, the passed pointer and localized GPR/MMX/x87 behavior.
The focused Python run passed all 12 tests. The complete generator run had
456 passes, 11 failures and one skip; its failure set is identical to the
previous recorded baseline, with no new failing tests.

NDK Android ARM64 O2/PIC compilation passed for the real fiber source, kernel
bridge and a complete generated chunk in grouped-only and argument modes.
The resulting `xgf_preempt_status_for_executor` contains zero `.tlsdesccall`
relocations, while `xgf_preempt_status` contains one. The actual generated
`sub_00011000` retains one TLS resolution in either mode; in argument mode
that address also supplies the executor argument. Thus this inspected caller
removes the query's additional resolution rather than moving a second one
into the caller. Other functions still need live profiling to quantify it.

Android ARM64 guest_fiber, guest_executor and guest_kernel_executor regression
binaries linked successfully with both experimental flags. They were not run
on the phone in this step. The executor regression now compares both query
entries in its native callback and priority/guard scenarios.

Sources copied for assembly inspection, assembly, compilation logs, host test
results and Android test binaries are under
`performance-results/2026-10-27/executor-tls-arg/` (local artifacts).

## User build and device check

The user handles APK builds, installation and game launch. Build the new mode:

```sh
./gradlew assembleDebug -Pnfsu.registerBlock=true -Pnfsu.executorTlsArg=true
```

Install `android/app/build/outputs/apk/debug/app-debug.apk`, open the same
ASUS race, and signal readiness before profiling. Retain executor=1, perf=1,
native_culling=0 and the same game settings/logging. Check game behavior and
capture executor resolver share, CPU and renderer flips/s. Preparation checks
above did not include a full APK build, lint or on-device gameplay; the later
user-built APK measurement is recorded below.

For a grouped-only comparison, omit `-Pnfsu.executorTlsArg=true` or explicitly
set it to false. Omitting both properties selects the original register mode.

Host query test (macOS):

```sh
cc -O2 -DRECOMP_REGISTER_BLOCK=1 -DRECOMP_EXECUTOR_TLS_ARG=1 \
  -ffunction-sections -fdata-sections -Ixboxrecomp/src -Itests \
  tests/guest_preempt_tls.c xboxrecomp/src/kernel/recomp_context.c \
  -Wl,-dead_strip -pthread -lm -o /tmp/nfsu-preempt-tls-host
/tmp/nfsu-preempt-tls-host
```

## ASUS race recording

The user built, installed and launched the new mode, then signalled readiness.
The installed APK matched the local package, SHA256
`6a77c85945e77253d3edc952c8ef8ab8d6ebb1f6151fa54015539da711e5f840`.
Both `NFSU2_REGISTER_BLOCK` and `NFSU2_EXECUTOR_TLS_ARG` were ON in its
CMake configuration. Runtime executor/performance counters remained 1,
native culling 0. No installation or restart was performed by the profiling
step.

A 25-second CPU-clock/1000 Hz recording on executor TID 18544, starting
27 October 2026 at 01:18:12 Moscow time, collected 12,336 samples with zero
losses. The screen after recording shows an active lap-knockout race, lap 2,
with three racers remaining. Four complete PERF5 windows wholly inside the
recording give the following exploratory comparison to the previous grouped
run without the argument:

| Metric | Grouped only, prior repeat | Grouped + executor argument |
|---|---:|---:|
| Executor CPU, % one core | 77.47 | 46.91 |
| Renderer CPU, % one core | 82.43 | 85.47 |
| APU CPU, % one core | 20.39 | 14.98 |
| Renderer flips/s | 44.50 | 39.33 |
| Dynamic TLS resolver self, % executor | 9.00 | 6.09 |
| Resolver attributed to safe-point query, % executor | 5.39 | 0.00 |
| Resolver attributed to generated `sub_*`, % executor | 2.67 | 4.99 |
| Safe-point bridge inclusive, % executor | 26.51 | 17.54 |

The new query is `xgf_preempt_status_for_executor`, reached through
`recomp_preempt_for_executor`. Its resolver caller category has no samples,
consistent with the inspected assembly's zero TLS descriptor calls. Generated
callers still resolve the register block and now supply the executor argument
from it. Their share must not be compared in isolation as a measure of total
TLS cost: attribution and the executor-time denominator both changed.
Inclusive bridge time overlaps query/resolver time and is not additive.

The targeted extra resolver lookup is removed. However, driving scenes, lap
and AI state differ, as do elapsed run time and temperature. Battery temperature
after this capture was 39.2 C, versus 37.7 C after the previous grouped capture.
Lower observed executor CPU is not a precise isolated patch speedup, and this
run does not demonstrate an FPS improvement: observed flips/s are lower.
It also does not establish power or heat reduction.

Raw profiles, matching native library, screen, complete logs, TLS caller
breakdown, function self/inclusive shares and windowed counters are saved in
`performance-results/2026-10-27/asus-register-block/executor-arg/`.
Caller and site period totals both match total resolver periods.
