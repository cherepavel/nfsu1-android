# Executor TLS investigation, 26 October 2026

This is an attribution of the existing race recording, not a new performance
comparison or a runtime change. Logging stays enabled and native culling stays
off. The recorded 54.73 flips/s and 96.39% executor CPU are the baseline
described in [the preemption report](android-preempt-fastpath.md).

## Evidence

Input: `performance-results/2026-10-26/preempt-fastpath/race/profile.data`,
executor TID 26579, 24,836 CPU-clock samples. Of these, 4,111 samples have
`[linker]tlsdesc_resolver_dynamic` as their leaf, or 16.55% of executor samples.
Attribute each such sample to its nearest recorded non-resolver caller;
weight by sample period. Do not count inclusive ancestors again.

| Nearest caller | % executor | % dynamic TLS samples |
|---|---:|---:|
| Generated `sub_*` functions, combined (516 symbols) | 11.11 | 67.11 |
| `xgf_preempt_status` | 3.80 | 22.94 |
| `nfsu_matrix_native` | 1.17 | 7.05 |
| All remaining callers | 0.48 | 2.89 |

Every resolver sample has a recorded caller; 15 samples (0.06% executor)
have caller symbol `unknown`, included in the remaining category. Attribution
depends on recorded unwinding and symbols; it is sampling, not a call count
or an exact cost per access. These categories partition resolver self time.
They must not be added to inclusive `recomp_preempt` time (17.81%).

The largest generated callers individually are `sub_001B2344` (0.66% executor
inside resolver), `sub_00018C30_gen` (0.58%) and `sub_001D4790` (0.50%).
TLS cost is spread widely rather than concentrated in one translated function.

## Code and assembly findings

`xgf_preempt_status` already reads kernel state directly from
`self->context.kernel`; it does not call `recomp_context_current()` on its
cached path. Its remaining TLS read is `guest_fiber.c`'s `running` executor
pointer. The saved matching ARM64 disassembly, `status-after.S`, has one
TLSDESC indirect call at file VA `0x10216a8`. All 943 resolver samples attributed
to this function point to that site. Merely caching a local context pointer
inside this function cannot remove that access: the executor pointer must
first be obtained.

The generated register ABI declares separate TLS symbols for GP registers,
XMM/MMX state, flags and FP stack. Declarations live in
`xboxrecomp/templates/runtime/recomp_types.h`, definitions in
`xboxrecomp/src/kernel/xbox_memory_layout.c`. Context switching explicitly
saves and restores these registers in `recomp_context.c`. Changing only the
current-context pointer would leave this generated-register traffic intact.

`nfsu_matrix_native` combines TLS property-poll state (`calls`, `enabled`)
with the register accesses in the inline `matrix_try_native()` body. Its
1.17% is the combined resolver cost, not evidence that property polling alone
costs that much. Disabling native culling also retains the wrapper's TLS
polling: its resolver share is 0.14% executor in this recording.

## Next experiments

1. Prototype a grouped TLS register block in an isolated build. Obtain its
   address once within a generated function, then address registers by offset.
   Inspect ARM64 output first to verify fewer resolver calls. Update generator,
   runtime declarations/definitions and context save/restore consistently.
   A pointer into a native thread's register block may survive a guest yield
   only if that native-thread ownership remains guaranteed; never retain a
   guest context pointer across a switch without reloading it.
2. Treat the safe-point executor pointer as a separate experiment with a
   measured 3.80-percentage-point resolver target in this capture. Any way of
   passing/caching it must cover native-backed threads, callbacks and executor
   lifecycle. A process-global `running` pointer would discard current TLS
   isolation. Do not change every TLS declaration or force a static TLS model
   without separately checking Android shared-library loading compatibility.
3. Inspect native matrix's resolver sites before changing its polling state.
   Preserve the live property switch and native-thread mode.

The percentages identify sampled cost, not predicted FPS gains or guaranteed
savings. Validate candidates with existing context/fiber/executor regressions,
then alternating device runs in the same reproducible race state, with the
same logging, culling setting and comparable temperature. Record executor and
renderer CPU, FPS, resolver share and audio/scheduler behavior. The previous
microbenchmark result remains separate from whole-game results.

## Reproduce

From the repository root, using Python and the installed NDK simpleperf module:

```sh
python3 tools/profile_executor_tls.py \
  performance-results/2026-10-26/preempt-fastpath/race/profile.data \
  --simpleperf-dir "$ANDROID_NDK_HOME/simpleperf" --tid 26579 \
  > performance-results/2026-10-26/preempt-fastpath/race/tls-callers.json
```

The JSON includes period totals, caller shares and individual file virtual
addresses. Conservation checks passed: caller and site totals both equal
4,111 resolver samples' periods, and total samples match the saved report.
No new device measurement was made during this investigation.
