# Android executor query without a fast-path stack frame

12 November 2026. Branch `codex/runtime-hotpath`, based on `develop`
`acb6381` after descriptor allocation reuse was merged (`e5678a7`).

## Fresh baseline

The installed APK matched the tested descriptor-reuse candidate, SHA256
`73e14a57e52a31cf2689eb9af2d10a8227d7a40028b725a04ec532ec8dafb895`.
The user resumed the paused race and confirmed driving before the separate
race capture. A 20-second CPU-clock/1000 Hz profile of game process 22517
recorded 46839 samples with zero loss. The screenshot after recording shows
an active third lap. No APK installation, restart, property change or game
input was performed by the profiler.

| Sampled thread | Samples | Relevant CPU share within that thread |
| --- | ---: | --- |
| Renderer 31221 | 15624 | Ordinary descriptor adapter 7.386% inclusive |
| Executor 31214 | 13869 | Argument preemption query 9.388% self / 10.787% inclusive |
| Executor 31214 | same denominator | Preemption bridge 11.500% inclusive; dynamic TLS resolver 4.305% self |
| APU 31233 | 6019 | Voice processor 60.359% self |

Inclusive entries overlap. These are sampled CPU shares, not GPU time,
expected savings or a controlled comparison to earlier races. In the current
session descriptor counters repeatedly report all requests reused and zero
new allocations after warmup. Remaining descriptor update/bind work is still
visible; the allocation stage has been removed from the warmed path.

The matching unstripped library, raw profiles, screenshots, logs, TLS caller
breakdown and analysis are saved locally under
`performance-results/2026-11-12/runtime-hotpath/`. The first `baseline/`
capture began on the pause screen and is not used for the race figures above;
the separate `race/` capture supplies the table.

## Finding and change

The baseline Android ARM64 `xgf_preempt_status_for_executor` allocated a
64-byte stack frame, saved registers, read the stack canary and checked it
on return, including for the cached no-peer case. Its inlined equal-priority
clock branch contained an address-taken `timespec`, so Android's existing
`-fstack-protector-strong` caused this work even when the clock branch was
not taken.

The clock comparison is now a separate noinline helper. The existing cold
ready-list refresh returns the final `XgfPreemptStatus` rather than returning
to a caller that needs another decision. The cached decision also returns
that enum directly. This lets the argument query tail-call either uncommon
helper and return directly for the usual cached case.

Inspection of the **final linked APK library** confirms that the argument
query contains no stack accesses or `bl` calls: the refresh and clock paths
are tail branches. The clock helper retains its stack-canary check. No stack
protection flag was disabled. The bridge entry and generated safe points are
unchanged. This is an implementation change, with no new property or mode.

All guard ordering, suspended/acquire load, generation/acquire load, cached
priority decisions, generation snapshot before refresh, and strict
`monotonic_ms() > slice_start` comparison are retained. Higher priority still
wins without reading the clock. Equal priority still reads the clock on each
eligible query. Native callback, GIL/no-preempt and dispatch guards are kept.
Native-thread ownership, lifecycle and TLS layout are unchanged; remaining
generated-register TLS lookups are not addressed by this change.

## Validation

- Extended query fixture passes host ASan/UBSan and Android ARM64. Its fake
  monotonic clock checks the instant before, at and after the quantum boundary,
  exact clock-read counts, guards, priorities, wake invalidation, TLS isolation
  and architectural-context ownership. The same extended fixture also passes
  against the saved production source from before this change on Poco.
- Actual ARM64 `guest_fiber`: 2000 yields, two native C stacks, nested locals,
  per-fiber longjmp/register/FP/kernel state and lifecycle pass.
- Actual ARM64 `guest_executor`: priorities/FIFO, 1000 messages, callbacks,
  native notifications, suspend/resume, deadlines and handle lifetime pass.
- Actual ARM64 `guest_kernel_executor`: real thunk waits/delays, GIL,
  contended recursive RTL, TryEnter/Leave, native thread preservation and
  abandonment pass. Its expected unsafe-boundary diagnostic is retained.
- `assembleDebug lintDebug` passes with register-block and executor argument
  enabled. Existing compiler/lint warnings remain.

## Isolated Poco CPU measurement

The actual before/after `guest_fiber.c` is compiled into separate PIC shared
libraries with O2, grouped-register/executor-argument flags, unwind tables,
FORTIFY and stack-protector-strong. A loader alternates library order for
three pairs in each scenario. A real guest fiber makes repeated argument
queries with GIL depth 1 and a warmed ready cache. Thread CPU time excludes
executor setup/destruction. No game draw or GPU work is measured.

| Cached scenario | Before, ns/query | After, ns/query | Less CPU time per query |
| --- | ---: | ---: | ---: |
| No ready peer, 20 million calls | 6.704–6.736 | 5.045–5.055 | about 25% |
| Higher-priority peer, 20 million calls | 6.266–6.302 | 4.404–4.411 | about 30% |
| Equal-priority peer with clock, 5 million calls | 61.176–61.207 | 58.207–58.311 | about 5% |

Quiet checksums are exactly 20000000, higher-priority exactly 40000000.
Equal-priority queries transition from STAY to YIELD as real time passes, so
their sums naturally vary; exact boundary semantics are checked separately
by the deterministic fixture. Normal clocks were used without CPU pinning
or frequency locking, while the game remained running. These are narrow
query measurements, not established game CPU/FPS/power improvements. The
refresh-heavy case is covered for correctness, without a performance claim.

The user installed the final APK and reports that gameplay looks normal.
This is a successful user gameplay check; no post-change game CPU/FPS profile
or separate outcome for each requested transition was reported. APK SHA256:
`fec36d99b485593a0907df6d3d6d6e93caad721700325ddce9f88df494e36062`.
Packaged and unstripped library build IDs match:
`fa055bcc667e813dec215f2e030e9fbe27df1612`.

## Reproduction

Save the previous source before changing it, then build Android fixtures:

```sh
git show develop:xboxrecomp/src/kernel/guest_fiber.c > /tmp/guest_fiber-before.c
python3 tests/guest_preempt_leaf.py \
  --cc "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang" \
  --reference /tmp/guest_fiber-before.c --output-dir /tmp/nfsu-preempt-leaf
adb push /tmp/nfsu-preempt-leaf/guest_fiber /tmp/nfsu-preempt-leaf/guest_executor \
  /tmp/nfsu-preempt-leaf/guest_kernel_executor /tmp/nfsu-preempt-leaf/guest_preempt_tls \
  /tmp/nfsu-preempt-leaf/guest_preempt_reference /tmp/nfsu-preempt-leaf/preempt-leaf-bench \
  /tmp/nfsu-preempt-leaf/libpreempt-reference.so /tmp/nfsu-preempt-leaf/libpreempt-candidate.so \
  /data/local/tmp/
adb shell /data/local/tmp/guest_preempt_reference
adb shell /data/local/tmp/guest_preempt_tls
adb shell /data/local/tmp/guest_fiber
adb shell /data/local/tmp/guest_executor
adb shell /data/local/tmp/guest_kernel_executor
adb shell /data/local/tmp/preempt-leaf-bench /data/local/tmp/libpreempt-reference.so \
  /data/local/tmp/libpreempt-candidate.so
```

Host query regression:

```sh
cc -O2 -DRECOMP_REGISTER_BLOCK=1 -DRECOMP_EXECUTOR_TLS_ARG=1 \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-sanitize-recover=all -Ixboxrecomp/src -Itests \
  tests/guest_preempt_tls.c xboxrecomp/src/kernel/recomp_context.c \
  -Wl,-dead_strip -pthread -lm -o /tmp/nfsu-preempt-leaf-host
/tmp/nfsu-preempt-leaf-host
```
