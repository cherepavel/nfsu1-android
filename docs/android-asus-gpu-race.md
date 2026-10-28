# ASUS GPU race investigation, 27 October 2026

The user restarted the existing APK with `debug.nfsu.gpu_profile=1` and opened
an ordinary race. No renderer change, build, install or restart was performed
by this collection step. The agent requested a single pass sequence with
`debug.nfsu.trace_target=race-gpu-0129`.

## Confirmed configuration

ASUS ZS671KS, game PID 30958, renderer TID 31032. Startup at 01:27:50 Moscow
time confirms timestamps mode 1, 48 valid bits and 52.0833 ns timestamp period.
Saved graphics options are 250%, FXAA on, square pixels on, anisotropy 16x,
reflections on, 60 FPS target and blur off. Alpha/zero-source specializations
are on; compact shader, deferred clear, unused-depth omission and scoped-pass
barrier experiments are off. Early-submit threshold is 300, native push
descriptors are off. These are observations, not newly selected policies.

Collection started with a log snapshot at 01:29:08; the pass-sequence request
was issued at 01:29:40 and captured frame 5400 at 01:29:46.674. Final logs and
screen were pulled at 01:30:16. Counters below use three complete GPU5 intervals
after the one-frame logging window: report ends 01:30:02.454, 01:30:07.469 and
01:30:12.492, totalling 717 completed frames. Means are frame weighted.

| Metric | Per-frame mean |
|---|---:|
| Guest GPU queue interval | 17.895 ms |
| Final output queue interval | 2.985 ms |
| Combined queue interval | 20.880 ms |
| Rendering instances, including final output | 21.03 |
| Full barriers | 25.65 |
| Blit commands | 25.57 |
| Guest indexed draws | 1963.3 |
| Guest indices | 206751 |
| Depth-attached passes | 16.03 |
| Nominal depth LOAD / STORE area | 17.707 / 17.707 Mpix |

Queue intervals include submission gaps and synchronization; these are not
isolated shader time or a hardware GPU-busy counter. Depth areas count command
policy, not physical memory traffic or driver compression. The detailed
timestamp instrumentation can perturb execution. This run does not establish
the cause of heating, an energy cost or a performance improvement.

## Where the work is recorded

The top-ten pass groups show the large 2133x1200 targets dominating recorded
pass intervals. The two alternating main targets are combined below; values
are contributions per completed frame, not time per invocation:

| Target size / end marker | Recorded interval contribution |
|---|---:|
| 2133x1200 / `kick_submit` | 4.421 ms |
| 2133x1200 / `cube_from_surfaces` | 2.783 ms |
| 2133x1200 / `vk_flip_impl` | 1.856 ms |
| 2133x1200 / `vk_draw_raw_impl` | 0.965 ms |
| 2133x1200 / `begin_rendering` | 0.734 ms |
| 2133x1200 / `tex_copy_from_ring` | 0.601 ms |

The table is limited to groups present in each GPU5 report's top ten, so it
is not a full partition of game time. An end marker identifies the function
that closed a pass, not the isolated cost of that function. In particular,
the 4.421 ms is mostly the preceding rendering, not measured submit overhead.

Frame 5400 has 19 guest passes (final FXAA is separate): a main-target clear,
five half-size target passes, eight reflection-face passes (two faces split
by `tex_copy_from_ring`), two main geometry passes separated by `kick_submit`,
another main pass ending before a lighting target, a lighting pass and the
main UI pass. The first main geometry segment has 431 draws and the next 464;
the additional UI segment has 135 draws / 542 indices. This records pass
splitting in the current race without guessing semantic roles from guest
addresses alone; lighting/UI labels also rely on the earlier target audit.

Source confirms:

- `stored_size()` scales stored surface width and height; square pixels add
  horizontal width for non-AA surfaces. Main targets here are 2133x1200;
  half-size targets 1067x600; reflection-face targets 427x320.
- `kick_due()` requests an early submit at the configured count, allowing
  twice the threshold inside a continuing pass. `kick_submit()` ends rendering,
  submits the command buffer and starts a new one, invalidating target/state
  caches. It retains ring resources and does not wait for a fence there.
- Reopening ordinary color/depth attachments uses LOAD and closing uses STORE.
  Whether the driver physically spills/reloads all of that is unmeasured.
- Texture upload also ends a pass. Reflection assembly copies changed faces
  and generates a cube mip chain. The blit counter includes mip blits, so
  25.57 blits does not mean that many full-screen copies.

## Next single-variable experiment

Compare the same race/quality with early submissions disabled:

```sh
adb shell setprop debug.nfsu.vk_kick 0
```

The user fully restarts the game and signals readiness. No rebuild is required;
leave GPU profiling at 1 and retain the same graphics/CPU settings. Verify a
new PID/TID and startup `threshold 0`. Collect a new pass trace and warmed
reports. Compare pass counts, depth LOAD/STORE area, guest draws/indices,
combined queue intervals and CPU/presentation counters. Different driving
scenes still limit an isolated timing comparison.

This tests the tradeoff between earlier CPU/GPU overlap and fewer forced pass
splits. A previous menu barrier experiment did not demonstrate a benefit from
narrowing barriers; it does not answer this race-specific submit comparison.
Earlier Retroid runs also do not establish a best threshold for this ASUS.
No speedup from disabling early submits is assumed.

Restore the original policy with `adb shell setprop debug.nfsu.vk_kick 300`
and a manual process restart. Any promising change needs return/repeat and
verification with detailed GPU timestamps disabled before a default is changed.

Raw complete logs, screen, capture times and weighted summaries are local under
`performance-results/2026-10-27/asus-gpu-race/`. Battery temperature was 38.0 C
at the initial snapshot and 38.7 C at the end; this is not a thermal comparison.

## First race with early submits disabled

The user changed `debug.nfsu.vk_kick` to 0, fully restarted and signalled a
race. PID 7396 / renderer TID 7459 started at 01:35:01; startup explicitly
confirms threshold 0 and GPU profile mode 1. Saved quality options match the
previous run. Runtime executor/performance counters remain 1, native culling 0.
No build/install/restart was performed by the agent.

The new sequence token `race-k0-0136` was issued at 01:36:40 and captured frame
4800 at 01:36:41.950. The selected comparison uses the same offset rule as
the earlier capture: complete GPU5 intervals within request time +15 to +35
seconds. This gives report ends 01:37:03.570, 01:37:08.578 and 01:37:13.594,
723 frames. Later logs are retained but not mixed into these means. The final
pull/screenshot occurred at 01:38:24 and shows loading; that screenshot does
not establish the appearance of the earlier measured race segment. The earlier
trace and user readiness message establish race operation near the sample.

| Metric | Kick 300 | Kick 0 |
|---|---:|---:|
| Guest queue interval, ms/frame | 17.895 | 16.428 |
| Final output interval, ms/frame | 2.985 | 3.032 |
| Combined queue interval, ms/frame | 20.880 | 19.461 |
| Render instances/frame | 21.03 | 19.34 |
| Full barriers/frame | 25.65 | 23.64 |
| Blits/frame | 25.57 | 25.01 |
| Guest draws/frame | 1963.3 | 2039.7 |
| Guest indices/frame | 206751 | 226460 |
| Depth-attached passes/frame | 16.03 | 14.34 |
| Nominal depth LOAD / STORE, Mpix each | 17.707 | 15.089 |
| Renderer flips/s, separate PERF5 windows | 48.20 | 47.27 |
| Renderer CPU, % one core, PERF5 | 94.69 | 96.52 |
| Executor CPU, % one core, PERF5 | 70.55 | 73.14 |
| Queue-submit calls per renderer flip, PERF5 | 4.27 | 1.00 |

PERF5 rows use three complete windows in the same selected wall interval;
their boundaries and frame counts differ from GPU5 and are not exact shared
per-frame denominators. Both profiles are ordinary moving races, not repeated
fixed scenes. Draw/geometry workload differs. Battery temperature at initial
kick-0 snapshot was 38.2 C and at final pull 39.0 C, without a continuous
frequency/thermal trace.

The structural change is confirmed: `kick_submit` no longer ends passes. In
the traced frame one main geometry segment reaches 1904 draws before the
reflection-assembly boundary. Texture uploads still split reflection-face
passes. This does not attribute all of the preceding segment's elapsed time
to the eventual upload or reflection assembly operation.

The selected combined queue interval is about 1.42 ms (6.8%) lower, with fewer
passes and lower nominal attachment load/store area. Renderer flips/s do not
improve and renderer CPU is slightly higher. This is a useful direction for
a return control, not a demonstrated FPS/thermal fix or a reason to change
the default. Next compare a manually restarted return to kick=300 with the
same timestamps/settings, then verify any promising result without detailed
timestamps. No additional experimental renderer policy is combined with it.

Raw kick-0 data, screenshot, capture times, pass groups and GPU/CPU summaries
are in `performance-results/2026-10-27/asus-gpu-race-kick0/`. The reusable local
summary script is `asus-gpu-race/summarize.py`; it filters renderer TID and
requires each whole report interval to fit inside the requested bounds.


## Follow-up audit: remaining splits and ASUS renderer CPU

The user chose to continue from the structural kick=0 result rather than make
a return run a prerequisite. Keep kick=0 as the working experiment; the global
default remains unchanged. The following is an investigation, not a measured
optimization or a claim about power savings.

### A large avoidable-looking boundary

In the existing kick=0 PASSSEQ frame 4800, passes 6–13 render the six reflection
faces. Two uploads split the first face into three rendering instances. Pass
14 then renders **1904 draws / 144180 indices** to the 2133x1200 main target
before `cube_from_surfaces` ends it. Pass 15 reopens that same main target for
217 more draws. Thus all six face renders precede the main geometry; lazy cube
assembly at first sampling is what introduces this particular main-target
boundary. The sequence supports moving the assembly earlier as an experiment.
The 6.599 ms/frame top-ten group ending at cube assembly measures preceding
rendering, not the cost of assembling the cube.

Source path: `vk_draw_raw_impl` calls `bind_stage` before `begin_rendering`;
`bind_stage` discovers rendered cube faces and calls `cube_from_surfaces`.
That helper compares each face's source pointer/generation, calls
`color_ready`, ends rendering, barriers, blits changed faces into a square
cube image, builds its mip chain, then barriers again. It already skips
unchanged face copies. `gen_mips(..., layers=6)` processes all six layers;
all six faces were rendered in the traced frame, so dirty-layer mip generation
alone has no demonstrated saving here. The blits also resample 427x320 faces
to square cube layers, so replacing them blindly with image copies would
change the operation.

The next renderer experiment should prepare an already-known cube at a
natural target transition **before** the main rendering starts, keeping lazy
assembly as a correctness fallback. It must rediscover current surfaces by
address (surface slots can be evicted/reused), preserve clear materialization,
size changes, generation invalidation and resource lifetime, and never read a
face before its preceding guest draws/clear. No assumption that a future face
will remain unchanged may replace the existing generation check at sampling.
Only refresh when sources changed; avoid speculative work for unused cubes.
That last constraint requires care: eager assembly on every target switch
could do more work than the existing lazy path. A useful first experiment
needs counters for early refresh, fallback refresh, changed faces and extra
refreshes, alongside PASSSEQ and visual checks. It should be opt-in.

Removing this boundary could avoid one main attachment STORE/reopen-LOAD pair.
For 2133x1200 and 4-byte color plus 4-byte depth/stencil this is nominally
40.954 MB of attachment area per occurrence, counting both STORE and LOAD.
This is bookkeeping, not measured DRAM traffic; tile retention, compression,
cache behavior and driver scheduling are unknown. It is not a bandwidth or
thermal saving claim.

`tex_copy_from_ring` is a separate issue: uploads must precede the first draw
that samples the uploaded texture. Deferring them without tracking consumers
is invalid. Pre-uploading would require visibility into upcoming batches and
resource updates; removing its barrier alone does not remove its required
outside-rendering copy. Prioritize the cube boundary before that larger change.

### CPU sample on this ASUS, not the previous Poco/Mali

A 20-second renderer-only simpleperf capture used the existing process 7396,
TID 7459, cpu-clock at 1000 Hz with DWARF callchains. The process did not
restart: PID 7330 returned by the bare package lookup is the launcher.
Recording reports 19.9805 seconds, 19314 samples, lost=0, equivalent to 19.314
seconds sampled CPU. ELF build ID in the recording is
`b9600256040d65da1a97ec6aa3eff9390ffa8014`, matching the unstripped ELF retained
in `asus-register-block/executor-arg`. Symbols were resolved using that cache.

The screenshot taken during capture shows the **race pause menu**, not active
driving. This capture diagnoses the rendering path in that scene; it must not
be used as a moving-race FPS comparison. GPU detailed profiling and existing
performance counters remained enabled; no properties, quality settings, APK,
process state or game input were changed.

| Inclusive renderer CPU samples | Share |
|---|---:|
| `vk_draw_raw_impl` subtree | 60.53% |
| `android_push_descriptors` subtree | 19.64% |
| Adreno `vkAllocateDescriptorSets` subtree | 9.16% |
| Adreno `vkUpdateDescriptorSets` subtree | 7.31% |
| Adreno `vkCmdBindDescriptorSets` subtree | 2.20% |
| Adreno `vkCmdDrawIndexed` subtree | 5.06% |
| `clock_gettime` subtree | 14.69% |
| `perf_clock` subtree | 7.38% |
| `tex_get` subtree | 3.41% |

Rows overlap and must not be added. Clock callers include 7.29 percentage
points directly under `vk_draw_raw_impl` and 7.38 through `perf_clock`;
inlining obscures individual `hz_now` callsites. Source has unconditional
program/texture timing clocks in the draw path as well as PERF scopes.
This is evidence of measurement/accounting overhead, not a promise that all
14.69% can be removed. A clean timing experiment needs detailed GPU timestamps
and per-call PERF counters disabled, while retaining an external presentation
measurement. Existing hitch clocks can still remain after those switches.
The present 96% utilization therefore does not by itself prove how much CPU
headroom a non-instrumented build has.

Descriptor allocation/update remains a candidate, but existing native-push
and dynamic-descriptor experiments must be consulted before repeating them;
previous menu tests do not establish the best racing policy. No descriptor
policy was enabled in this audit.

Raw profile, screenshot, build IDs, resolved CPU report, clock-caller summary
and complete performance log are under
`performance-results/2026-10-27/asus-kick0-followup/`. No renderer code change,
build or install was performed. The concrete next GPU experiment is cube
assembly placement; the CPU result also requires a clean external measurement
before interpreting renderer saturation as the reason FPS failed to rise.


## Opt-in implementation

`debug.nfsu.cube_early=1` (or `RECOMP_VK_CUBE_EARLY=1`) enables
transition-time assembly; read once per process, default off. Only cached
cubes with all six source generations changed, the same source identities
and stored size qualify, while leaving a face for a non-face draw target.
First use and all partial/late updates keep the original lazy assembly.
Disabling cube maps suppresses the eager path. All copy, mip, clear and
barrier operations use the existing assembly function.

`[VKCUBE]` reports cumulative early builds, lazy builds, early results
subsequently used, and same-frame lazy rebuilds following an early build.
The used counter includes cases requiring a later refresh; read it together
with fallback counts. Early builds exceeding uses can indicate speculative
work (also allow for an outstanding cube at the capture boundary). This
experiment can do unwanted work for an unused cube; that is why it is opt-in.

Host ASan/UBSan scheduling regression covers face-to-face rejection, missing
and reused surface slots, partial updates, size changes and no repeat after
generation synchronization. Deferred-clear regression also passes. Device
visual/pass/time validation is pending; the user will build and install.

Keep `debug.nfsu.vk_kick=0` and `debug.nfsu.gpu_profile=1` for the initial pass
comparison. Set `debug.nfsu.cube_early=0` and restart to restore lazy assembly.

Preserve the executor configuration used by the measured APK when building:

```sh
./gradlew assembleDebug -Pnfsu.registerBlock=true -Pnfsu.executorTlsArg=true
```

Both Gradle properties default to false; omitting them changes another variable
in addition to cube assembly. The user builds, installs and launches the game.


## First ASUS race with early cube assembly enabled

User built/installed/launched with both executor Gradle flags retained, then
signalled a race. No agent build, install, restart or game input was performed
in this capture. Game PID 22091, renderer TID 22148; startup at 01:51:35 and
`early assembly=1`, kick threshold 0, GPU profile 1 confirmed. Saved options
remain 250%, reflections/FXAA/square pixels on, 16x anisotropy, 60 FPS and blur
off. Screens at the beginning/end show active driving and reflected detail;
they do not establish temporal/pixel-equivalent visual correctness.

PASSSEQ request `cube-early-0153` was set after the 01:52:36 state snapshot and
recorded at 01:52:37.442, frame 3000. Its 17 guest passes include two upload
splits, the last reflection face ending at `cube_from_surfaces`, then a
**single main geometry pass of 1129 draws / 109088 indices** ending at the
natural target change. Previously, frame 4800 had main geometry split into
1904 + 217 draws by cube assembly. This confirms the intended relocation and
removal of that large-target boundary. The total difference of one traced
pass includes different draw workloads/upload activity and is not a fixed
per-frame pass-count guarantee. Final output is separate from PASSSEQ.

Selected reports are complete GPU5 intervals inside 01:52:51–01:53:11, after
the tracing/screenshot window: ends 01:52:57.824, 01:53:02.828, 01:53:07.840,
630 frames. Frame-weighted means:

| Metric | Previous kick=0, lazy cube | Kick=0, early cube |
|---|---:|---:|
| Game queue interval, ms | 16.428 | 17.848 |
| Output interval, ms | 3.032 | 3.062 |
| Combined queue interval, ms | 19.461 | 20.910 |
| Rendering instances/frame | 19.34 | 18.39 |
| Full barriers/frame | 23.64 | 22.75 |
| Blits/frame | 25.01 | 25.18 |
| Guest draws/frame | 2039.7 | 2394.2 |
| Guest indices/frame | 226460 | 268044 |
| Depth-attached passes/frame | 14.34 | 13.39 |
| Nominal depth LOAD / STORE area, Mpix each | 15.089 | 12.584 |
| Renderer flips/s (separate PERF5 windows) | 47.27 | 42.56 |
| Renderer CPU, percent of one core | 96.52 | 97.23 |
| Executor CPU, percent of one core | 73.14 | 72.55 |
| Queue submits / renderer flip | 1.00 | 1.00 |

PERF5 rows use whole independent windows within those bounds, not the exact
GPU frame denominator. The moving scenes differ: new draws +17.4%, indices
+18.4%; thermal/frequency conditions are not controlled. There is no measured
speedup in this run and these numbers do not establish a slowdown caused by
the patch either. Nominal depth area falls approximately one main attachment's
worth, but this is not a measurement of DRAM traffic or power. Battery was
40.0 C at start and 40.7 C at final snapshot; no thermal conclusion follows.

From frame 2100 through 4800 counters grow by 2700 early builds and 2700 early
uses; lazy builds remain 1 and same-frame fallbacks remain 0. The normal sampled
racing portion therefore uses its eager result without a late cube rebuild.
However, cumulative early builds minus uses increased by **429** before frame
2100 (276 + 153 across two 300-frame intervals). That gap then stays flat.
The first implementation can eagerly refresh a cube even while the scene does
not sample it. These are genuinely additional speculative assemblies; the
specific early scene/loading transition was not screen-recorded.

### Guard added after this capture (not yet installed or measured)

Actual calls from `bind_stage` now record `sampled_frame`. Eager assembly only
qualifies if that cube was sampled in the current or previous frame; eager
assembly itself cannot refresh this eligibility. When sampling stops this
allows at most one subsequent frame of speculation per cube, then leaves
assembly to the original lazy path until real use resumes. The generation,
size and transition checks remain. First use is still lazy. This guard is a
new source revision: the results above describe the user-installed first
revision, not this unmeasured guard.

Host scheduling tests now also check no prior use, expired use, current use
and frame-counter wrap with ASan/UBSan; they pass. Android compiler syntax
check passes (existing unused-symbol warnings). No full APK build or device
validation of the guard was performed; the user handles builds/install/start.

Raw complete logs, options, screenshots, CPU/GPU summaries and state snapshots
are under `performance-results/2026-10-27/asus-cube-early/`. The first
`trace-request.txt` timestamp command returned only the date because ADB shell
split its format; the timestamped start snapshot and PASSSEQ log bound the
request instead. Selected report bounds above are explicit and reproducible.


## Camera menu, early assembly on, guarded revision

The user proposed a repeatable menu comparison and entered Options → Camera.
New process PID 26686 / renderer TID 26759, startup 01:55:28; VKCUBE source
line positions match the revised guard. Properties: cube_early=1, kick=0,
gpu_profile=1. Options remain 250%, FXAA/Square/reflections on, 16x, 60 FPS,
blur off. No build/install/process restart or game input by the agent.
Screens show the same Camera menu/car setting at the start/end; the car/menu
animation still makes this a repeatable animated scene, not a frozen frame.

Trace request after state snapshot 01:57:07 recorded frame 6000 at 01:57:10.397.
Six reflection-face instances end with the last face at cube assembly, followed
by one main scene instance, lighting target and UI. No main geometry split at
cube assembly. There are 15 guest instances plus final output = 16 total.

Whole GPU5 windows selected within 01:57:22–01:57:42 end at 01:57:30.082,
01:57:35.087 and 01:57:40.093, 899 frames. Weighted game interval 11.651 ms,
output 2.458 ms, combined 14.110 ms. Counters: 16 passes, 18 full barriers,
14 blits, 628.9 guest draws, 65212 indices, 11 depth passes and 11.698 Mpix
nominal depth LOAD/STORE area each. Separate whole PERF5 windows give
59.88 renderer flips/s, 39.97% renderer CPU and 26.36% executor CPU.
Battery snapshots both 39.2 C. No power measurement or frequency control.

From frame 6600 to 7800: +1200 early builds, +1200 uses; lazy remains 1,
same-frame fallback remains 0. Unlike the first racing launch, this menu
launch has no cumulative gap between early builds and uses at these snapshots.
This is live menu coverage, not proof the guard eliminates all speculation.

Recorded pass-group interval contributions (top-ten coverage only):
2133x1200 ending at target changes 5.012 ms/frame; main/UI ending at flip
2.893 ms; lighting target 1.291 ms. Final FXAA/output interval is 2.458 ms.
These intervals include command/synchronization effects; end-function names
are boundaries, not isolated operation costs. The three latter portions total
about 6.64 ms and are a substantial remaining investigation target after the
cube-placement comparison; do not attribute the entire menu load to cubes.

The user reports GPU remains heavily loaded even in the menu. This is not
dismissed as an inevitable result of 250% rendering. The current experiment
only moves assembly and removes one pass boundary; it retains the guest
rendering, reflection views, lighting, UI, FXAA and resource work. Kernel
sysfs gpuclk/gpubusy/devfreq reads were denied, so no GPU frequency or hardware
busy inference is made from the overlay percentage or queue timing. Previously
rejected shader/depth experiments should be consulted before repeating them.

Raw logs/options/screens/state and reproducible CPU/GPU summaries are under
`performance-results/2026-10-27/asus-cube-menu-on/`. Next collect the same
installed APK/menu at cube_early=0 after the user's manual restart, holding
kick/quality/profile fixed; no fresh build is required. This mode-on sample
alone is not evidence of a speedup over mode off.


## Camera menu comparison: early assembly on → off

The user disabled cube_early and manually restarted, then returned to the same
Camera menu. PID 29871 / renderer TID 29928, startup 02:00:05; startup explicitly
confirms `early assembly=0`, threshold 0 and GPU profile 1. Saved options are
byte-identical to the on sample. No agent build/install/restart/input or quality
change occurred. Capture start 02:01:08, trace frame 3900 at 02:01:12.533.

The off trace has 16 guest instances plus final output = 17. Six faces finish,
then main geometry records 215 draws and is closed by cube assembly; the same
main target reopens for 73 draws before the lighting target. This is precisely
the boundary absent from the on trace, rather than an eliminated guest draw.

Whole GPU5 intervals inside 02:01:23–02:01:43 end 02:01:32.219, 02:01:37.224,
02:01:42.229, 900 frames. The earlier on selection has 899 frames. Means:

| Metric | Lazy (0) | Early (1) |
|---|---:|---:|
| Game queue interval, ms | 12.221 | 11.651 |
| Output queue interval, ms | 2.457 | 2.458 |
| Combined queue interval, ms | 14.678 | 14.110 |
| Rendering instances | 17 | 16 |
| Full barriers | 19 | 18 |
| Blits | 14 | 14 |
| Guest draws | 628.2 | 628.9 |
| Guest indices | 65031 | 65212 |
| Depth-attached instances | 12 | 11 |
| Nominal depth LOAD/STORE Mpix each | 14.258 | 11.698 |
| Renderer flips/s, separate PERF5 windows | 59.93 | 59.88 |
| Renderer CPU, percent of a core | 40.17 | 39.97 |

The observed combined interval difference is **-0.568 ms/frame (-3.87%)** with
only +0.11% draws and +0.28% indices in the on sample. All three on windows
(14.074–14.133 ms) are below all three off windows (14.630–14.736 ms), and
output timing is virtually unchanged. Structural reduction is one pass,
one barrier and 2.560 Mpix nominal depth load/store area, with identical blit
count. This supports this optimization candidate more directly than the earlier
moving races, whose workloads differed substantially.

It remains one chronological on→off comparison with detailed timestamps enabled,
not repeated randomized trials or external presentation/power measurement.
CPU/GPU clocks were not controlled; battery snapshots were 39.2 C on and
39.5 C off. Car/menu animation is visible, so images are not pixel-identical
frozen scenes. No general FPS/thermal gain or physical DRAM saving is claimed.
The two runs both remain at the 60 FPS target. The entire original menu load
is not explained or fixed by a roughly 0.57 ms interval reduction.

Raw off artifacts live under `performance-results/2026-10-27/asus-cube-menu-off/`;
comparison data is `performance-results/2026-10-27/asus-cube-menu-comparison.json`.
The GPU summarizer is `asus-gpu-race/summarize.py`; the reusable whole-window
CPU summarizer is `asus-cube-menu-on/summarize_cpu.py`. The device remains at
cube_early=0, kick=0, profile=1 after this capture. Shipping default is unchanged.
The next larger audit should inspect the menu/UI and full-screen passes;
prior depth/compact shader negative experiments must not be blindly repeated.


## Next command-state experiment prepared

The subsequent UI/presentation audit found unconditional Android blend/stencil
state setters on every draw. An opt-in live `debug.nfsu.state_cache` comparison
and counters were implemented, with production-helper mock-driver ASan/UBSan
checks and Android syntax checks including both user executor flags. No APK
build/install or device timing of this new path has occurred. Full rationale,
lifetime/invalidation audit, prior negative experiments and same-process
comparison protocol are in [vulkan-dynamic-state-cache.md](vulkan-dynamic-state-cache.md).
The phone's properties and game process were not changed by that implementation.


## Dynamic-state installed comparison completed

The same-process Camera sequence 0→1→0→1 completed on PID 3729 / TID 3865.
Setters drop from about 2510 to 4/frame, but combined GPU intervals remain
14.687 ms off / 14.688 ms on and UI remains 2.899 / 2.901 ms. Renderer CPU
falls from 40.58% to 39.98% of one core; this small repeated CPU effect does
not resolve the GPU load. Actual state_cache=0 was restored. Full windows,
counter interpretation, minor music-banner workload caveat and raw artifact
locations are recorded in [vulkan-dynamic-state-cache.md](vulkan-dynamic-state-cache.md).

The user subsequently chose to retain the cache: state_cache=1 was reapplied
and confirmed, and the Android source default is now on. This does not change
the measured absence of a GPU benefit. The next opt-in experiment separates
UI rasterization from scene supersampling; implementation, costs, visual
differences and the pending device comparison are recorded in
[vulkan-native-ui.md](vulkan-native-ui.md).
