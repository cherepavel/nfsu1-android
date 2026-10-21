# Experimental render-pass consumer scope

2026-10-21. Hypothesis for ASUS Zenfone 7 Pro / Adreno 650, Options → Camera:
`begin_rendering`'s ALL_COMMANDS → ALL_COMMANDS barrier may serialize the next
pass's vertex/binning work behind previous fragment work. This is a concrete
dependency in the source, **not a measured explanation of heating or 90% GPU**.

The [Khronos pipeline-barrier sample](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_barriers/README.html)
describes preserving vertex/fragment overlap with narrower consumer stages.
It motivates testing this renderer; it does not predict an Adreno speedup.

## Change and dependency audit

Android `debug.nfsu.pass_barrier=1` opts in; unset/0/invalid retains the original
barrier. Polling occurs at the first pass and at least 300 flips after the last
check. `[VKBARRIER] scoped_pass=N frame=...` in **performance.log** confirms
actual selection. The default remains off, pending visual and timing evidence.

Only the normal barrier immediately before a new guest rendering instance
changes. Both modes issue one barrier and reset the same surface dirty flags.
No draw, clear, image, attachment load/store, shader, descriptor, render area,
sampling setting or pass count is deliberately removed/changed.

| Dependency | Source scope retained | New consumer scope |
| --- | --- | --- |
| Previous attachment write → fragment texture read | ALL_COMMANDS / MEMORY_WRITE | FRAGMENT_SHADER / SHADER_READ |
| Shared color, blending, loads and writes | same | COLOR_ATTACHMENT_OUTPUT / COLOR_ATTACHMENT_READ+WRITE |
| Shared depth/stencil including load/store | same | EARLY+LATE_FRAGMENT_TESTS / DEPTH_STENCIL_ATTACHMENT_READ+WRITE |
| Upload/clear/blit → rendering | same, includes transfer writes | same fragment/attachment consumers |
| Previous fragment sample → attachment overwrite (WAR) | ALL_COMMANDS execution scope includes the sample | attachment stages wait |

No BY_REGION flag is used: a shader may sample different pixels from those it
writes. Source execution/access scopes remain unchanged, including work in
earlier command buffers/submissions on the same queue. Full barriers before
transfers, cube assembly, readback and presentation remain unchanged. Pending
depth transfer clears still use their two original full barriers.

The generated vertex shader (`gl_vsh.c`) reads host-written attributes and UBOs
from the per-frame staging ring; it has no sampled/storage images, SSBOs,
atomics, or GPU writes. The renderer has no compute/indirect draws or GPU
geometry generation. Host ring storage survives early submits; frame reuse
and ring-overflow reuse wait on fences. Thus no preceding GPU write consumed
by vertex work requires the old destination scope. Adding GPU-produced vertex
data or vertex texture fetches requires revisiting this assumption.

GENERAL layouts and STORE/LOAD bandwidth remain candidates. This experiment
does not establish that they are cheap, nor justify dropping stored contents
or merging the menu's fullscreen draws, which may implement required blends.

## Timing without per-pass timestamps

`debug.nfsu.gpu_profile=2` at process startup creates/resets only three queries:
frame start, guest end and final output end. It records no per-pass timestamps
and emits no GPUPASS table. Mode 1 retains the previous detailed profiler;
mode 0 disables profiling. Startup `[GPU] ... mode=2 pass_timestamps=0` confirms
the coarse mode. Queue intervals still include submission gaps, waits, and the
remaining timestamp commands. They do not measure busy percentage or energy.

GPU5 now includes `pass_barriers` for the experimental barriers;
`full_barriers` continues to mean ALL_COMMANDS → ALL_COMMANDS. Compare their
**sum**, not a fall in the latter alone. `guest_draws` and `guest_indices` count
actual indexed commands (indices × instances), including held instancing
flushes, in both profiling modes. They exclude final non-indexed FXAA.

## Manual-install comparison

The user signs/installs `android/app/build/outputs/apk/debug/app-debug.apk`.
Do not install/uninstall it or change the package/path. Before the manual
launch, set only:

```sh
adb -s LAAIB7606721VPZ shell setprop debug.nfsu.gpu_profile 2
adb -s LAAIB7606721VPZ shell setprop debug.nfsu.pass_barrier 0
```

Keep kick=300, alpha_specialize=1, zero_source=1, framebuffer_cache=1,
compact_psh=0 and lazy_clear=0. Launch the new process; verify these properties
against startup logs (`[GPU]`, `[vkkick]`, `[VKSPEC]`, `[VKCLR]`, `[VKFB]`).
Open Options → Camera, retaining 250%, 60 FPS, FXAA, Square Pixels,
reflections and 16x anisotropy, Blur/Bloom off. Warm the scene for 30 seconds.
Record the current launch time, PID, renderer TID and option lines. Logs append
across launches: never treat a tail of yesterday's log as the current process.

Hold baseline 0, then switch **0→1→0→1** in this one process with:

```sh
adb -s LAAIB7606721VPZ shell setprop debug.nfsu.pass_barrier 1
# Next stage: use 0, then 1 again, waiting for each actual VKBARRIER marker.
```

Allow at least 60 seconds per stage without interacting with the menu. Exclude
report ends within 15 seconds of every actual mode marker (including initial
scene warmup separately). Keep passes, blits, total barriers and guest draws/
indices comparable; animated geometry can vary slightly. Reject mixed scene,
setting changes, pauses, compilation, acquisition/thermal transients. Record
device thermal/battery state and short SurfaceFlinger latency samples alongside
each stage. The GPU clock remains inaccessible through the available shell.

Pull performance/graphics/system logs including rotations when needed into
`performance-results/2026-10-21/asus-menu/`. Process and capture each launch
separately. Weighted summaries:

```sh
python3 tools/summarize-pass-barrier.py performance-results/2026-10-21/asus-menu/performance.log \
  --tid RENDERER_TID --since '2026-10-21TLAUNCH_TIME' \
  --sample-after '2026-10-21TSTABLE_CAMERA_TIME'
```

The tool reports separate segments, frame-weighted game/present/total intervals
and workload counters. It drops the first five seconds of report ends after
`--sample-after`, keeping loading/navigation out of the baseline. It does not
infer that the scene/settings stayed fixed
or automatically claim a speedup. Require reproducible return/repeat differences
in **total** queue interval plus unchanged appearance. The 2026-10-20 numbers
used profiling=1 and kick=0, so they are not this experiment's baseline.

Use timestamped screenshots for visible corruption checks, supplemented with
guest target dumps if needed. The car is animated, so pixel differences between
live screenshots alone do not prove an error or equivalence. Check reflections,
fog, lighting, UI/text, depth ordering and several rotations of the car. Follow
with racing and lifecycle checks if this menu result merits enabling the mode.

Restore `pass_barrier=0` (live) and `gpu_profile=0` (next restart). Recheck any
promising result with profiling disabled; preserve quality/FPS settings.

## Validation so far

- `tests/vulkan_pass_barrier.c` tests the production barrier helpers with a
  mock driver: original default, exact producer/consumer masks, non-regional
  dependency, surface visibility, counters, property polling and live rollback.
  Host and simulated Android policies pass ASan/UBSan.
- `tests/vulkan_deferred_clear.c` retains shared-depth/partial-clear coverage
  and checks that coarse mode counts a pass without allocating a pass slot or
  writing a query beyond its three-entry pool. Both policies pass ASan/UBSan.
- `./gradlew assembleDebug lintDebug` passes. APK SHA-256 before the user's
  signing: `0b78b7f2e98ae7cc8822e21b67737813e781b71bb37378668b52623aedbe1699`.
- The summary tool's synthetic smoke check passes: launch/TID filtering,
  initial-scene exclusion, mode warmup, frame weights and separate return
  segments. This validates parsing, not a performance result.
- These tests check recording/policy. They do **not** establish driver behavior,
  pixel equivalence, live performance or a reduction in heating. Those remain
  pending the user's manual APK installation and the controlled comparison.

At preparation the phone had no running game process; original properties
matched the handoff (profile=0, kick=300, compact=0 and existing fixes on).
For the next manual launch only profile was set to 2 and pass_barrier to 0.
The original phone snapshot is saved in the ignored results directory as
`device-before.txt`. No APK was installed and no app was started by the agent.

## Live comparison with kick=300

The user manually installed the APK and opened Options → Camera. One process,
PID 14542 / renderer TID 14697, started 2026-10-21 11:32:23 MSK. Startup logs
confirm mode=2/pass_timestamps=0, kick=300, alpha=1, zero_source=1, compact=0,
lazy_clear=0. Current option lines confirm the requested quality settings.
The screenshot shows ASUS's dimmed touch-lock overlay over the Camera menu;
it was left in place throughout the comparison. This differs from an ordinary
unobscured run and limits visual checks. No performance inference uses old TIDs.

The initial summary samples report ends from 11:36:00 onward. Each subsequent
mode segment excludes report ends within 15 seconds of its actual VKBARRIER
marker. Results are weighted by completed frames:

| Mode | Windows | Frames | Game ms | Final output ms | Total ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 17 | 5100 | 14.5224 | 2.1465 | 16.6689 |
| 1 | 19 | 5699 | 14.5270 | 2.1471 | 16.6741 |
| 0 return | 19 | 5699 | 14.5297 | 2.1427 | 16.6724 |
| 1 repeat | 16 | 4800 | 14.5271 | 2.1438 | 16.6709 |

All segments have 17 passes, 14 blits and 19 total barriers. In mode 1,
16 barriers use the narrower consumer scope and 3 remain full. Actual indexed
draw counts average 628.7–632.0/frame and indices 65,123–65,250/frame; the car
continues animating. No workload reduction or repeatable timing benefit is
demonstrated. The source change remains disabled by default.

The total interval is almost exactly the 60 Hz period. This is consistent with
queue/submission/pacing gaps dominating the measured interval; it is not proof
that both modes execute the same GPU instructions or consume the same energy.
The profiler starts before early-submitted work and ends after the final
submission, so CPU-side gaps between those submissions remain in the interval.

SurfaceFlinger exposes only 24/46/25/33 valid actual-present timestamps in the
four short snapshots. A1 and B2 each contain one ~33.16 ms gap; B1/A2 have
maximum gaps ~16.80 ms. These short, partly populated buffers do not support
a sustained FPS/pacing improvement in either mode. HAL thermal status stayed
0; GPU0 HAL readings at B1/A2/B2 ends were 49.9/51.5/52.3 °C. The gradual
warming, unlocked GPU frequency and sparse samples prevent an energy/thermal
comparison. The vendor's implausible `soc` value is not treated as a temperature.

Raw logs, screenshots, thermal/SF snapshots, `barrier-live-summary.json` and
`barrier-sf-summary.json` are ignored artifacts under
`performance-results/2026-10-21/asus-menu/`. The helper invocation used:

```sh
python3 tools/summarize-pass-barrier.py performance-results/2026-10-21/asus-menu/barrier-live-performance.log \
  --tid 14697 --since 2026-10-21T11:32:23 --sample-after 2026-10-21T11:35:55
```

After collection pass_barrier was restored to 0 and the live marker confirmed
the rollback at 11:42:41.215. For a diagnostic repeat, **vk_kick was set to 0
for the next process**, while profile remains 2 and pass_barrier 0. PID 14542
still uses its startup kick=300; changing the property does not alter it.
No installation is needed for that repeat. After a complete user restart and
return to the same scene, verify threshold 0 and collect a new 0→1→0→1 sequence.
Keep it separate from this run. Restore kick=300 and profile=0 after collection;
neither startup policy is restored in an already-running process.

## Repeat without early submissions (kick=0)

The user restarted and returned to Camera. PID 21467 / renderer 21596 started
at 2026-10-21 11:47:22 MSK. Startup logs confirm kick=0, profile=2 and the same
specialization/clear flags and quality settings. The touch-lock overlay was
absent; Game Genie was visible. This run is analyzed independently of kick=300.
A mixed-scene report at 11:48:44 is excluded; sampling starts at report ends
after 11:49:22. Each mode transition again excludes the first 15 seconds.

| Mode | Windows | Frames | Game ms | Final output ms | Total ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 19 | 5697 | 11.8489 | 2.1519 | 14.0008 |
| 1 | 23 | 6898 | 11.8020 | 2.1403 | 13.9423 |
| 0 return | 19 | 5700 | 11.8168 | 2.1435 | 13.9603 |
| 1 repeat | 15 | 4500 | 11.8484 | 2.1507 | 13.9991 |

The first pair differs by -0.0585 ms, but returning/repeating gives the opposite
direction (+0.0388 ms). **No reproducible benefit** from narrowing this barrier
is demonstrated even after removing early-submission gaps. It remains off.
This does not rule out other unnecessary dependencies or attachment traffic.
All four segments retain 17 passes, 14 blits and 19 total barriers. Indexed
draws average 628.2–631.4/frame; indices 65,094–65,240/frame. The user reported
no change in appearance or smoothness; saved screenshots have no obvious
corruption, but animated live images cannot establish pixel equivalence.

Short SurfaceFlinger samples contain 6/6/47/41 valid actual-present timestamps.
A1/B2 each have one ~33.12 ms gap; the other two snapshots have no >25 ms gap.
These sparse samples establish no sustained pacing difference. GPU0 HAL
readings were 54.2 °C initially, 55.3 at B1, 56.1 at A2 and 55.7 at B2;
thermal status stayed 0. This is not an energy or controlled thermal result.

Logs, mode-separated weighted results, screenshots and SF/thermal snapshots
use the ignored `barrier-k0-*` names under the same 2026-10-21 results folder.
The summary command uses TID 21596, since 11:47:22, sample-after 11:49:17.
Pass barrier was restored to 0. The following unused-depth experiment uses a
new APK; its next-launch diagnostic settings are documented separately in
[vulkan-unused-depth.md](vulkan-unused-depth.md).
