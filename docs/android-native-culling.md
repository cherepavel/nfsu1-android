# Executor profile and opt-in native bounds test

26 October 2026. Diagnostics/logging retained. The first sample started while
the game was paused and is not the basis for race conclusions. After the user
restarted a race, a separate 30-second cpu-clock/1000 Hz DWARF profile of
executor TID 14937 / PID 11313 recorded 28,464 samples, lost=0.

Selected inclusive/self percentages of executor sampled CPU:

| Symbol | Inclusive | Self |
|---|---:|---:|
| linker tlsdesc_resolver_dynamic | 16.09% | 16.09% |
| xgf_preempt_status | 15.79% | 7.42% |
| fiber_should_preempt | 4.83% | 3.57% |
| sub_00018C30 | 5.55% | 3.32% |
| sub_00018B40 | 1.41% | 1.13% |
| nfsu_matrix_native | 1.91% | 1.17% |

Inclusive entries overlap; do not add them. TLS is a shared cost of translated
register/state accesses, not a pure arithmetic routine. Safe points cannot be
removed without preserving scheduler semantics. Existing matrix NEON work is
already present. The profile supports investigating bounds/frustum arithmetic,
not a claim that all remaining executor load is useful game math.

## Candidate

sub_00018C30 tests a bounds box against six planes, returning outside / partial /
inside. An optional transform delegates to sub_00018B40. `culling_native.h`
implements the zero-transform ordinary-low-RAM case using local scalar C
arithmetic. It deliberately preserves double-operation order, float rounding
of intermediate values, all GP results, x87 register slots/top/comparison,
frame bookkeeping and touched guest stack bytes. No fast-math or FMA.

Strange ranges, scratch-stack aliases, nonzero transforms, non-finite/large
inputs and nonstandard constants use the generated body. Only low 64 MiB
addresses are currently accepted, so actual coverage is not assumed. Do not
expand pointer eligibility without verifying memory/alias semantics.
Deferred guest-stack stores rely on rejecting aliases with the scratch frame;
the successful path contains no guest call or yield inside its arithmetic.
Exceptional fallback can repeat a finite arithmetic prefix in the generated
path; masked host FP exceptions are assumed, as elsewhere in this runtime.

Startup/default property `debug.nfsu.native_culling` is **off** unless exactly 1.
It is re-read every 65,536 calls in TLS, so a live change is delayed. Once
active, `[CULLNATIVE] hits=... fallback=...` reports every 262,144 calls.
The original generated function is retained as `_gen`. Enabled execution
keeps its entry safe point; disabled execution uses the original safe point.
Native fallback after an enabled safe point may encounter a second safe point
in the original body. This cost must be included in live measurements.

## Validation and limits

`tests/culling_native.py` extracts the actual generated function into an
independent reference TU. 32,000 cases compare all observed GP/FPU state,
stack scratch/canaries, FP exception flags, eight x87 top positions, four
rounding modes, stack alignments and outside/partial/inside outcomes.
Host ASan/UBSan and on-device ARM64 pass. Additional guards check transform,
invalid/MMIO/end-of-range pointers, stack alias and non-finite fallback.
This is not proof for all FP bit patterns or whole-game image equivalence.

Final isolated device timing, one million calls/pass, alternating three pairs:

| Case | Generated CPU ms | Native CPU ms |
|---|---|---|
| Full six planes | 264.003 / 262.226 / 263.706 | 229.107 / 229.155 / 229.285 |
| Reject first plane | 131.268 / 129.691 / 130.991 | 135.112 / 135.516 / 135.711 |

Thus full traversal is ~13% cheaper but early rejection is ~4% dearer.
The microbenchmark excludes Android property checks, enabled fallback and
real preemption contention. It is not a whole-executor or FPS saving.
First prototypes that retained stack-based intermediates were slower or
nearly equal and were rejected. The candidate remains off by default.

`./gradlew assembleDebug lintDebug`: BUILD SUCCESSFUL. APK built at the usual
path but not installed. Next test must establish hit/fallback coverage, real
branch distribution and full-path cost before retaining/enabling this change.
A ~5.55% inclusive hotspot cannot explain or remove the whole executor load.

Raw data and prototype timings: local ignored
performance-results/2026-10-26/executor-native/.

## First installed on/off race check: candidate rejected for now

Installed APK SHA256 05d79d6234799ad7c2262d91226075f87f5c5aab99b65d8dbef28eb9ccebb991
verified against the built candidate. PID 15697 / executor TID 22760 remained
the same. Live property 1 then 0; each capture requested 20 seconds of cpu-clock
at 1000 Hz, diagnostics retained. On recorded 19,238 samples; off 18,807.

On: entire sub_00018C30 subtree 6.39%, including nfsu_culling_native 3.27%
and generated fallback 3.09%. Off: entire subtree 5.34%, including generated
5.10% and disabled wrapper 0.19%. Inclusive entries overlap. Native hit counters
in the on interval show 3,636,908 accepted and 1,868,116 fallback calls (~66%).

This pair does not establish a benefit; the combined path has a higher sampled
share when enabled. Scenes and frequencies are not controlled, so do not label
the percentage difference a precise slowdown. Full-path guard/fallback costs
matter more than the isolated full-six-plane microbenchmark. The property was
left at 0 after the control sample; no restart was needed. Do not enable this
candidate by default or claim a CPU/FPS/thermal gain. Next optimization needs
better coverage and lower wrapper/guard costs, or a different target.

Raw captures: local culling-live/on and culling-live/off directories.
