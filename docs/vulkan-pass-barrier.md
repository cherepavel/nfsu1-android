# Experimental render-pass consumer scope

The Android RC now enables the scoped barrier when its property is unset or
empty. Explicit 0 or invalid nonempty values select the original barrier;
clearing an override restores the scoped default. Desktop remains unchanged.
See [RC configuration](android-rc-build.md). The dated experiments below
describe their historical defaults and captured settings before this rollout.

2026-10-21. Hypothesis for ASUS Zenfone 7 Pro / Adreno 650, Options → Camera:
`begin_rendering`'s ALL_COMMANDS → ALL_COMMANDS barrier may serialize the next
pass's vertex/binning work behind previous fragment work. This is a concrete
dependency in the source, **not a measured explanation of heating or 90% GPU**.

The [Khronos pipeline-barrier sample](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_barriers/README.html)
describes preserving vertex/fragment overlap with narrower consumer stages.
It motivates testing this renderer; it does not predict an Adreno speedup.

## Change and dependency audit

Android `debug.nfsu.pass_barrier=1` selects scoped consumers; unset/empty now
also selects them. Explicit 0/invalid retains the original barrier. Polling
occurs at the first pass and at least 300 flips after the last
check. `[VKBARRIER] scoped_pass=N frame=...` in **performance.log** confirms
actual selection. It was default-off during the historical captures below.

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

## Poco Camera follow-up, 2026-10-28

The user authorized checking submission gaps and barriers after the reflection
copy timing. Poco 24095PCADG, PID 10873 / renderer TID 12593 / executor TID
12590; startup 15:12:15 MSK. Actual kick=0, early cube assembly=1, native UI=0,
lazy_clear=0, state cache=1. This run deliberately retains detailed profile=1
and cube timestamps=1 from the preceding kick comparison; it is **not** the
coarse-profile protocol above. Camera remains at 100%, FXAA/reflections/square
on, aniso16, fps60, blur0. Options and TRACE environment are byte-identical
across captures. No renderer policy/source change, build, install, restart or
game navigation was performed by the agent during this live comparison.

First live marker: mode 1 at 15:18:28.986, frame 22200. Return to mode 0 at
15:20:04.096, frame 27900. Select whole GPU5 report intervals inside the
explicit boundaries below, excluding fifteen seconds after actual markers.
CPU rows use independently complete PERF5 windows. Each early stage contains
seven GPU5 windows; A1/B1 have 2100 frames and A2 has 2099.

| Stage | Selected interval MSK | Game ms | Output ms | Total ms | Cube assembly ms |
| --- | --- | ---: | ---: | ---: | ---: |
| A1, original | 15:17:43–15:18:23 | 4.0214 | 2.6076 | 6.6290 | 0.1744 |
| B1, scoped | 15:18:43.986–15:19:23.986 | 3.4587 | 2.5997 | 6.0584 | 0.1736 |
| A2, original return | 15:20:19.096–15:20:59.096 | 4.0557 | 2.6197 | 6.6754 | 0.1757 |

The first pair differs by -0.5706 ms total and returning to original adds
0.6169 ms. Draws average 632.29 / 624.50 / 624.42 and indices 65134 / 65112 /
65085; animated/instanced draw counts differ, while index workload is close.
Renderer flips/s are 59.9428 / 59.9402 / 59.9136 and CPU 66.76 / 66.13 /
66.38 percent of one core. This is a promising local queue-time result,
not an FPS, energy or isolated GPU-instruction improvement.

All selected reports retain 16 instances, 13 image blits, 11 depth instances
and 1.872 nominal depth LOAD/STORE Mpix each. There is one queue submit per
flip. Original mode uses 18 full barriers; scoped mode uses 3 full plus 15
scoped, retaining 18 total. Every completed frame has one measured cube build,
six face copies and seven mip blits, with no missing/overflow groups. Screenshots
at the sampled stages show Camera without obvious corruption; they do not
establish temporal/pixel equivalence or racing/lifecycle correctness.

### Later repeat and state drift

Wireless ADB disappeared and returned under a second mDNS name. The attempted
connection to the old direct IP endpoint was refused; it did not establish an
extra IP transport. The game PID/TIDs stayed unchanged. A1's current log had
rotated; its dated rotation was retained and prepended before summarization.

During the longer interruption, timings changed globally while mode was still
0: final output rose to about 6.16 ms and cube assembly to about 0.354 ms,
roughly twice their earlier values, with the same command counters/settings.
This predates B2's mode-1 marker at 15:25:26.889, frame 39000. It cannot be
attributed to that switch. Clocks are not controlled/measured, so the cause
is not established. Later thermal service reports status 0 and current HAL
GPU 48.919 C, battery 37 C; this snapshot neither explains the drift nor
establishes a cooling result.

Exploratory adjacent controls were added after observing this drift. A3 uses
the fixed interval marker(B2)-45 seconds through marker(B2)-5 seconds. A4
uses the same +15-second warmup / 40-second selection after final rollback.
These post-hoc controls are kept separate from the first A1/B1/A2 cycle.

| Stage | Windows / frames | Game ms | Output ms | Total ms | Cube ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| A3, original before B2 | 7 / 2100 | 8.3469 | 6.1586 | 14.5054 | 0.3540 |
| B2, scoped, incomplete repeat | 5 / 1501 | 7.3473 | 6.2699 | 13.6172 | 0.3640 |
| A4, original after rollback | 7 / 2100 | 8.3361 | 6.1466 | 14.4827 | 0.3539 |

B2's planned 15:25:41.889–15:26:21.889 selection contains a presentation
rebuild at 15:26:10.063. Its five complete report intervals end by 15:26:07.219;
the next report ends only at 15:27:02.319 with 133 frames and spans a lifecycle
gap, so is excluded. Presentation logs also show rotation changes/rebuilds
at 15:24:00.434, 15:24:09.912, 15:24:22.062 and 15:27:02.318. The complete
B2 repetition is therefore not a clean confirmation, despite the lower
local interval relative to its adjacent exploratory controls. Do not pool
the early and late regimes or publish one general speedup percentage.

Final rollback is confirmed at 15:27:24.868, frame 42900, mode=0. The shipping
default remains off. At 15:34:19 only next-launch profiling was prepared:
gpu_profile=2, cube_profile=0, with kick=0, cube_early=1 and pass_barrier=0.
PID 10873 continues its startup detailed/cube profiling until the user fully
restarts it. No new APK is needed. Recheck the barrier cycle in the coarse
mode while keeping Camera foreground and orientation fixed; verify actual
startup mode=2/pass_timestamps=0/cube_timestamps=0. This confirmation remains
pending, as do profiling-disabled performance, racing and visual validation.

Raw snapshots, dated rotation, exact stage selections, helper and per-stage
GPU/CPU/cube summaries are in the ignored directory
performance-results/2026-10-28/poco-submit-analysis/pass-barrier/.

## Poco coarse-profile confirmation, 2026-10-28

The user fully restarted and returned to Camera. PID 12619 / renderer TID
21493 / executor TID 21468; GPU startup at 15:36:58.543 confirms mode=2,
pass_timestamps=0, cube_timestamps=0, 64 valid bits and 76.9231 ns period.
Kick remains 0, early cube assembly 1, native UI 0, lazy clears 0 and runtime
executor 1. Camera still uses 100%, reflections/FXAA/square on, aniso16,
fps60 and blur0. Options and TRACE environment are byte-identical across
all four stages and the end capture. No build, installation, restart or game
input was performed by the agent. This is a separate process from the earlier
detailed run; only within-process mode comparisons support the result below.

A1 selects already steady menu reports before the initial screenshot, with
the expected workload checked in every report. Each subsequent selection
starts fifteen seconds after its actual VKBARRIER marker and spans thirty
seconds; only complete GPU5 intervals are included. Screenshots and thermal
snapshots are taken outside these selected intervals. Capture helpers reject
a changed PID, graphics/env mismatch or presentation rebuild during a stage.

| Stage | Selected interval MSK | Windows / frames | Game ms | Output ms | Combined ms |
| --- | --- | ---: | ---: | ---: | ---: |
| A1, original | 15:38:01.259–15:38:41.259 | 7 / 2100 | 4.1520 | 2.5819 | 6.7339 |
| B1, scoped | 15:40:02.091–15:40:32.091 | 5 / 1499 | 3.4380 | 2.5820 | 6.0200 |
| A2, original return | 15:41:32.205–15:42:02.205 | 5 / 1500 | 4.1196 | 2.5996 | 6.7192 |
| B2, scoped repeat | 15:43:22.329–15:43:52.329 | 5 / 1500 | 3.4606 | 2.5826 | 6.0432 |

The first pair's combined interval falls by 0.7139 ms; the return adds
0.6992 ms, and the second on pair falls by 0.6760 ms. Pooling complete frames
within each mode gives game interval 4.1385→3.4493 ms and output
2.5893→2.5823 ms, a 0.6962 ms lower combined elapsed queue interval. This
reproduces the local benefit without per-pass/cube timestamps, unlike the
earlier incomplete detailed repeat. Queue timing still is not isolated GPU
busy/instruction time, and three frame timestamps remain enabled.

All 22 selected windows retain 16 rendering instances, 13 blits, 11 depth
instances and 1.872 nominal depth LOAD/STORE Mpix each. Original mode uses
18 full barriers; scoped mode uses 3 full plus 15 scoped, retaining 18 total.
Each mode uses one queue submit per flip. Draws average 624.33 / 623.97 /
636.58 / 624.50 and indices 65099 / 65040 / 65314 / 65097; the animated,
instanced scene is comparable rather than identical. Separate complete
renderer PERF5 windows give 59.9380 / 59.9011 / 59.9403 / 59.9465 flips/s and
46.46 / 46.93 / 48.85 / 47.86 percent CPU of one core. No renderer FPS or
consistent CPU reduction is established at the 60 FPS cap.

The graphics log contains only the startup presentation selection: rotation=1,
logical 2712x1220 and physical buffer 1220x2712. There are no subsequent
VKROT rebuilds, changed processes or lifecycle gaps through the end capture.
No GPUPASS/GPUCUBE records appear for this renderer/launch, confirming the
coarse mode is actually active. Stage screenshots retain the car/reflections,
lighting and menu text without obvious corruption; these are not pixel or
temporal correctness tests. Frequency was not controlled, and thermal service
snapshots do not establish an energy/heating reduction or transfer to ASUS/250%.

Final rollback is confirmed at 15:45:12.463, frame 29400, mode=0. At the
15:46:35 end capture the same PID remains active with gpu_profile=2, kick=0,
cube_early=1 and pass_barrier=0. No shipping default or barrier source changes
were made. The scoped mode is now a reproducible menu candidate on this Poco;
race/visual/lifecycle checks and profiling-disabled performance confirmation
remain before recommending it as a general default. The user can enter racing
in this process without rebuilding or restarting for the next validation.

Raw stage logs, screenshots, options/environment, thermal snapshots,
exact selections, capture/summary helpers and comparison.json are under
performance-results/2026-10-28/poco-submit-analysis/pass-barrier-coarse/.

## Poco race check, 2026-10-28

The user entered racing in the same PID 12619 / renderer TID 21493. Coarse
profile=2, cube timestamps=0, kick=0, early cube assembly=1, executor=1,
native UI=0 and lazy clears=0 remain unchanged. Graphics remain at 100%,
reflections/FXAA/square on, aniso16, fps60 and blur0. Options and the retained
TRACE environment are byte-identical across all captures. Only the existing
live barrier property is switched. No source policy/default, build, install,
process restart or game input is performed by the agent.

Actual markers are scoped=1 at 15:50:54.058 / frame 49500, scoped=0 at
15:53:43.134 / frame 59400, and scoped=1 at 15:55:28.254 / frame 65700.
Selections use whole GPU5 report intervals, with fifteen seconds excluded
after each actual switch. CPU rows use independently complete PERF5 windows.

| Stage | Selected interval MSK | Windows / frames | Game ms | Output ms | Draws | Indices | Renderer flips/s |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| A1, original, moving race | 15:49:16.967–15:49:56.967 | 7 / 2049 | 7.4459 | 3.7657 | 2424.95 | 261806 | 58.4143 |
| B1, scoped, moving race | 15:51:09.058–15:51:39.058 | 5 / 1465 | 5.9340 | 3.5686 | 2145.78 | 244350 | 58.5513 |
| A2, original, stopped car | 15:53:58.134–15:54:28.134 | 5 / 1500 | 6.0426 | 3.2534 | 1757.56 | 198052 | 59.9719 |
| B2, scoped, stopped car | 15:55:43.254–15:56:13.254 | 5 / 1500 | 5.0038 | 3.2540 | 1755.40 | 198063 | 59.9374 |

A1/B1 is a functional moving-race check, not a matched performance comparison:
B1 has about 11.5% fewer draws and 6.7% fewer indices on different road
segments. Its shorter interval does not isolate a barrier gain or establish
an FPS improvement. During B1 the user was explicitly asked about flicker,
missing objects and reflection/lighting errors, and answered that everything
looked normal. Screenshots show the road, car, lighting and HUD without
obvious corruption, with progression from lap one into lap two.

Later images show the car stopped at 0 MPH in the same position across A2/B2;
the race clock and HUD keep updating. This is an exploratory same-position
race pair. Game interval falls by 1.0388 ms, output differs by +0.0006 ms,
and combined interval falls from 9.2960 to 8.2578 ms. Draw count differs by
-0.12%, indices by +0.0054%; FPS and renderer CPU (74.83→74.72% of one core)
remain effectively unchanged. Instances are 17.76→17.78, blits 21.86→21.72,
and nominal depth LOAD/STORE area 2.253→2.226 Mpix each. Thus the work is
close, not identical, and clocks are uncontrolled. This single static pair
supports the menu candidate but is not a repeated matched racing benchmark
or an isolated GPU busy/energy measurement.

In A2 there are 21.56 full barriers; B2 uses 4.78 full plus 16.78 scoped,
retaining 21.56 total. Both use one queue submit per flip. Transfer/streaming
work remains and accounts for more blits/barriers than the menu. Independently
bounded VKCUBE counters from 15:49:15.375 to 15:56:08.307, frames
43800→68100, show +24300 early builds and uses, lazy builds fixed at 2 and
same-frame fallbacks fixed at 0. This counter span differs from the selected
GPU5 windows and is not reported as their exact frame coverage.

The PID remains unchanged and the graphics log still contains only its startup
VKROT selection; there are no presentation rebuilds in these race checks.
The user's temporal observation and screenshots are useful functional evidence,
not validation-layer, pixel-equivalence, long-session or pause/resume coverage.
Profiling-disabled performance, controlled energy/thermal measurements and
other devices/scales remain unvalidated. No universal shipping default changes.

The Poco is left with pass_barrier=1 for continued experimental play,
kick=0 and cube_early=1. At 15:59:30 gpu_profile was set to 0 for the next
manual game launch; cube_profile remains 0. Current PID 12619 retains its
startup coarse timestamps until restart, which the agent did not perform.
This does not alter graphics or require another APK. Ordinary play after a
future restart can check the candidate without GPU timestamp instrumentation;
that confirmation is still pending.

Raw logs, original/on/return/repeat screenshots, user observation, immutable
selections, workload/CPU summaries, cube counter spans and the separate
stationary-comparison.json are in the ignored directory
performance-results/2026-10-28/poco-pass-barrier-race/.
