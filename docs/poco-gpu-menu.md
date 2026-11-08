# Poco GPU menu investigation, 28 October 2026

The user manually restarted into Options → Camera on 24095PCADG. PID 18328,
renderer TID 22380, executor TID 22375; startup 13:33:35 MSK. Detailed GPU
profiling is confirmed enabled, with 64 valid bits and 76.9231 ns timestamp
period. Runtime executor=1, UI experiment=0, state cache=1, kick=300 and early
cube assembly=0. Saved graphics: scale=100, FXAA/reflections/square on,
anisotropy=16, fps=60, blur=0. This is a separate device/scale from ASUS 250%.
The existing TRACE environment was retained. The agent did not build, install,
restart or navigate the game.

## Baseline evidence

Five complete GPU5 windows wholly within 13:34:51–13:35:19 contain 1499 frames:
game queue interval 11.0013 ms and final output interval 2.6112 ms, combined
13.6125 ms. There are 17 rendering instances, 19 full barriers, 13 image blit
commands and about 626 indexed draws per frame. These are queue intervals,
including submission/pacing gaps, not GPU busy measurements. Hardware Mali
frequency/utilization files were unavailable or permission denied.

The Camera screenshot confirms the scene/settings. SurfaceFlinger's actual
game layer is DEVICE, bufferTransform=0; the display reports no client
composition in this snapshot. Startup pre-rotation is active: logical
2712x1220, physical buffer 1220x2712. No redundant compositor rotation pass
is observed in this run.

The steady one-frame sequence at frame 6900 has 16 guest instances plus final
output. The first instance clears the 853x480 main target and closes with zero
draws. Later, after the reflection faces, main geometry records 226 draws;
cube assembly closes that pass, and the same target reopens for 73 draws.
Lighting uses a separate 853x480 target, then the main target reopens for
197 UI draws / 790 indices. Top-ten report groups average about 0.536 ms/frame
for the UI instance, 1.015 ms for main geometry ending at cube assembly,
0.842 ms for main-target instances ending at target changes (includes the
initial clear), and 0.316 ms for the lighting target. Coverage is incomplete;
these sums do not equal whole queue time or isolate function execution costs.

Source and the steady blit count agree: six face-to-cubemap blits followed by
seven mip blits, each across six layers, for a 171-square cube. The mip sizes
are 85, 42, 21, 10, 5, 2, 1. This count is caused by the port's separate face
images and generated reflection mip chain. It is not 13 redundant copies of
the full scene; removing mip levels would change reflection filtering.

## Same-process deferred-clear check

The clear-only main instance is a concrete reason to test the existing
default-off deferred-clear mode on this Mali menu. Earlier ASUS video tests
reduced pass counts without establishing useful GPU gains; that result is
not assumed to apply to this device/scene. No new code or quality setting is
introduced here. Source behavior and mock/driver validation are documented in
[vulkan-deferred-clears.md](vulkan-deferred-clears.md).

Baseline A1 selects complete windows in 13:36:49–13:37:19. The live property
was set to 1 at 13:37:29, and the actual VKCLR marker is 13:37:33.124,
frame=14100. B1 selects complete windows in 13:37:48–13:38:18, excluding the
mode-transition window and 15 seconds of warmup. Each selection has five
GPU5 windows and 1500 completed frames.

| Mode | Game ms | Output ms | Total ms | Instances | Full barriers | Blits | Draws | Indices |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A1, original | 10.9110 | 2.6182 | 13.5292 | 17 | 19 | 13 | 624.26 | 65078 |
| B1, deferred | 11.3010 | 2.6896 | 13.9906 | 16 | 18.28 | 13 | 623.36 | 64937 |
| A2, original return | 11.1586 | 2.7046 | 13.8632 | 17 | 19 | 13 | 623.76 | 65026 |

Nominal depth load/store area falls from 2.281/2.281 Mpix per frame to
1.228/1.8652. These are attachment-area counters, not physical memory traffic.
A stable B1 pass sequence at frame 24900 confirms the main clear merges into
the later geometry instance. A clear-only reflection face instead materializes
at color_ready without depth; the other five have draws. The property request
initially captured the preceding baseline frame, so that earlier sequence was
not treated as proof of the new policy.

B1's total queue interval is about 0.46 ms higher despite one fewer instance.
The property was returned to 0 at 13:40:58; actual VKCLR confirms mode=0 at
13:41:03.361, frame 26700. A2's five complete GPU5 windows in
13:41:19–13:41:49 again contain 1500 frames and restore the original
pass/barrier/attachment counters. Options and trace environment are
byte-identical to the initial snapshot. The A2 interval is about 0.13 ms
below B1 but 0.33 ms above A1. Thus this is not a demonstrated optimization;
chronological drift and the single on segment limit causal interpretation.
The experimental policy remains off. The animated scene, unlocked clocks,
timestamp instrumentation and varying geometry prevent an energy, thermal
or sustained FPS claim. No dedicated B1 screenshot or lifecycle/visual stress
validation was performed, and no default policy is being changed.

## Next prepared comparison: early reflection assembly

The main-pass split at cube assembly remains present. The existing
cube_prepare_transition path can assemble recently used, fully updated cubes
at the final face-to-scene transition before the main scene begins. This
retains the copies, mip chain, guest rendering and fallback generation checks;
it targets the scene STORE/LOAD boundary rather than removing reflection
quality. The ASUS result is documented in android-asus-gpu-race.md and is not
assumed to transfer to this Mali/100% configuration.

At 13:44:02 the agent set debug.nfsu.cube_early=1 for the next user restart.
The current process still uses its startup cube_early=0. Keep kick=300,
gpu_profile=1, executor=1, ui_native=0, lazy_clear=0 and all graphics options
fixed. After the user restarts into Camera, confirm actual early assembly=1,
request a fresh pass sequence and compare whole queue time, main-pass split,
passes/barriers, blits, draws/indices and the early-use/fallback counters.
The mode-on result is pending; no GPU/thermal improvement is claimed.

## Early reflection assembly: installed menu result

The user restarted into Camera. New PID 18709 / renderer TID 24700 / executor
TID 24694, startup 13:45:42. Startup confirms early assembly=1, lazy_clear=0,
native UI=0, profile=1 and the same pre-rotation. Options and TRACE environment
are byte-identical to the preceding off process. No agent build/install/input
or graphics change occurred. Unstable wireless ADB required reconnecting to
the same already-authorized device; the game process stayed unchanged.

Compare the last retained stable off interval, 13:43:20–13:43:50, with the on
interval 13:47:05–13:47:35. Each contains five complete GPU5 windows / 1500
frames. Screenshots and the fresh pass-sequence request were collected after
the on timing selection. Data/selection/metrics are stored in
cube-off-summary.json, cube-early-on/summary.json and cube-comparison.json.

| Metric | Lazy assembly | Early assembly |
| --- | ---: | ---: |
| Game queue interval, ms | 11.1040 | 9.0856 |
| Output queue interval, ms | 2.6222 | 2.7110 |
| Combined queue interval, ms | 13.7262 | 11.7966 |
| Rendering instances | 17 | 16 |
| Full barriers | 19 | 18 |
| Blit commands | 13 | 13 |
| Indexed draws/frame | 623.38 | 625.48 |
| Indices/frame | 64924 | 65357 |
| Depth-attached instances | 12 | 11 |
| Nominal depth LOAD/STORE Mpix, each | 2.281 | 1.872 |
| Renderer flips/s, separate PERF5 windows | 59.94 | 59.94 |

The combined queue interval is **1.9296 ms lower (14.06%)** in this comparison,
with +0.34% draws and +0.67% indices. This is an observed queue result rather
than an isolated GPU busy/instruction/energy reduction: early submission
remains enabled, so command-recording gaps and scheduling remain in the timed
interval. The retained off intervals vary; this is one chronological off/on
pair without a Poco return/repeat or controlled clocks. No general percentage
FPS/CPU/thermal improvement is established.

The stable pass sequence at frame 15000 confirms the intended structure:
the last reflection face closes at cube assembly, then the main target has
one geometry instance (315 draws in that animated frame) ending at kick_submit,
followed by lighting and UI. There is no main-geometry instance ending at
cube assembly and no second geometry instance before lighting. The separate
initial clear-only main instance remains, since lazy_clear was restored off.

From frame 4800 to 6300, early builds and uses each rise by 1500, lazy builds
remain 1 and same-frame fallbacks remain 0. Counters continue matching through
the later retained menu snapshots. Thus the candidate actually runs and its
results are used in this menu, rather than merely changing a property.
The menu screenshot retains the car, reflections and text without obvious
corruption; moving images are not a pixel-equivalence check and this Poco
run does not cover racing/lifecycle stress.

The device is left at cube_early=1, executor=1, lazy_clear=0, ui_native=0 and
state cache enabled. GPU profiling remains 1 for the investigation. The
source shipping default for early assembly is still off; no new APK was built.

## Follow-up race validation

The user entered a race in the same process, PID 18709 / renderer TID 24700.
Options remain 100%, reflections/FXAA/square on, aniso16, fps60 and blur0;
executor=1, cube_early=1, ui_native=0 and lazy_clear=0. Start/end screenshots
show racing output and progression into lap two, with no obvious corruption.
They do not establish temporal correctness or pixel equivalence.

Five complete GPU5 windows wholly within 14:35:46–14:36:16 contain 1495 frames:
game interval 11.5183 ms, output interval 5.1095 ms, combined 16.6279 ms;
19.56 rendering instances, 22.68 full barriers, 19.36 blits, about 2498 draws
and 278805 indices per frame. Separate complete PERF5 windows give 59.73
renderer flips/s (window range 59.19–60.15), renderer CPU 83.76% of one core,
executor CPU 65.59%, and 0.0225 ms fence wait per flip. These queue intervals
include scheduling/submission gaps; output is not an isolated FXAA cost.

From frame 62700 to 64200, early builds and uses both rise by 1500; lazy
builds remain 3 and same-frame fallbacks remain 0. The one-frame sequence
near the capture closes the last face at cube assembly and has no main-target
instance ending there. Main geometry still splits at kick_submit, including
two instances of exactly 600 draws in that frame. Texture uploads can also
split reflection-face passes. These other boundaries remain investigation
candidates; this run does not compare kick thresholds.

The race confirms use of the early result without additional lazy/late cube
rebuilds in the selected interval. There is no matched off-mode race sample,
so neither a racing FPS gain nor a cooling/power improvement is claimed.
Raw logs, screenshots, exact selection and GPU/CPU/cube summaries are under
performance-results/2026-10-28/poco-gpu-race/.

The subsequent diagnostic separates rendered-face copies from cubemap mip
generation through optional cube_profile timestamps; see
[vulkan-gpu-profile.md](vulkan-gpu-profile.md). It does not change rendering
policy and remains disabled by default. The installed menu result follows.

## Reflection copy and mip timing

The user built and installed the diagnostic APK, then returned to Camera.
PID 26738 / renderer TID 10812 / executor TID 10811; renderer startup at
15:00:17 confirms gpu_profile=1, pass_timestamps=1, cube_timestamps=1,
64 valid bits and 76.9231 ns period. Early assembly remains enabled,
executor=1, native UI=0, lazy_clear=0 and kick=300. Start/end screenshots
confirm Camera at 100%, reflections/FXAA/square on, aniso16, fps60, blur0.
Options and the retained TRACE environment are byte-identical across the
capture. No agent build, install, restart, game input or mode change occurred.

Select complete report intervals wholly within 15:02:06–15:03:05, after the
start screenshot plus ten seconds and before the end screenshot. Eleven
GPU5 windows contain 3300 completed frames. Each GPUCUBE line is paired with
the preceding GPU5 report on the same renderer thread and checked for matching
frame count; logging can give the two lines timestamps one millisecond apart.

| Metric | Per completed frame |
| --- | ---: |
| Rendered-face copies and producer dependency | 0.0507 ms |
| Cubemap mip generation and dependencies | 0.1256 ms |
| Combined reflection assembly interval | 0.1763 ms |
| Builds / successfully timed builds | 1.000 / 1.000 |
| Face blits / mip blits | 6 / 7 |
| Missing / overflow measurement groups, whole selection | 0 / 0 |
| Game queue interval | 8.9698 ms |
| Final output queue interval | 2.6757 ms |
| Combined queue interval | 11.6455 ms |
| Rendering instances / full barriers / image blits | 16 / 18 / 13 |
| Indexed draws / indices | 625 / 65200 |

Across individual five-second windows, copy_ms is 0.050–0.051 and mip_ms
is 0.125–0.127. Values come from rounded report fields. The scoped sum is
about 1.97% of game_ms and 1.51% of the combined queue interval; it is already
included in game_ms. These ratios are not GPU busy or power percentages.
The scope excludes rendering the reflection faces and the preceding rendering
instance's STORE. It includes the assembly barriers, and does not measure
physical DRAM traffic. The old and new APKs are not an instrumentation-matched
performance comparison.

Separate complete PERF5 windows give 59.9409 renderer flips/s (range
59.9125–59.9640), renderer CPU 67.40% of one core, executor CPU 52.33%,
and 0.0469 ms fence wait per flip. From frame 6300 to 9600, early builds and
uses each rise by 3300; lazy builds remain 1 and same-frame fallbacks remain 0.
Thus every steady rebuild is measured and the early results are used, without
extra lazy rebuilds in the sampled menu. Screenshots show no obvious corruption,
but are not a temporal or pixel-equivalence test.

Direct face-to-cube rendering would target roughly 0.051 ms of scoped copy
work here before accounting for changed dependencies and render-store policy;
it cannot be justified as the main menu-load fix from this sample. The largest
reported guest group is the 853x480 depth-attached instance ending at
kick_submit, about 1.500 ms/frame and 296 draws. Final output is 2.676 ms,
including image readiness and scheduling effects. Pass timestamps exclude
work between passes, so neither the pass table nor the output interval alone
identifies the remaining GPU execution cost. Submission gaps, barriers and
pass boundaries remain candidates for the next investigation. No graphics
quality or renderer default is changed by this result, and it does not predict
ASUS/250% costs or a cooling improvement.

Raw logs, start/end scene images, process metadata, options/environment and
selection are under cube-timestamps-on/ in the ignored menu results directory.
summary.json contains GPU5/pass groups; cube-summary.json contains paired
GPUCUBE aggregates, coverage, CPU windows and early-build counter deltas.
The device retains cube_profile=1 for subsequent measurements.

## Submission-boundary follow-up: kick=300 baseline

The user authorized investigating kick_submit, barriers and submission gaps.
Source inspection confirms kick_due reads its environment/property once per
process: the default threshold is 300 non-instanced batches at a target/pass
boundary and 600 while continuing the same color/depth instance. A submit
ends rendering, starts a fresh command buffer and invalidates dynamic state;
it does not reset ring storage or wait for a fence. The final same-queue fence
protects all earlier submissions. No submission/barrier policy was edited.

One fresh PASSSEQ request was captured in the same PID 26738 / renderer 10812,
at 15:07:30.947, frame 25800. Main target 83240000 records 286 draws and ends
at kick_submit; the next instance uses **different** target 82A4ED80 for
seven lighting draws. Main 83240000 then returns for 197 UI draws. Thus in
this sampled menu frame the kick coincides with a necessary target boundary,
rather than splitting continuing main geometry. The race's 600-draw splits
documented above are a different case. A generic end=kick_submit label alone
does not prove an avoidable STORE/LOAD pair.

A fresh stable baseline wholly within 15:08:49–15:09:29, after sequence logging,
has seven complete GPU5 windows / 2100 frames. Game interval is 8.8620 ms,
output 2.6797 ms, combined 11.5417 ms; 16 instances, 18 full barriers,
13 blits, 624.69 draws and 65176 indices per frame. Options/environment are
byte-identical to the preceding cube-profile capture. Seven independent
complete renderer PERF5 windows give 59.9428 flips/s and 67.56% of one CPU
core. There are two vkQueueSubmit calls per flip, totaling 0.5566 host ms;
acquire is 0.3898, present 1.0253 and fence wait 0.0471 host ms per flip.
These host durations do not measure GPU idle intervals and cannot be
subtracted from GPU timestamps to reconstruct GPU execution time.

At 15:09:38, debug.nfsu.vk_kick was set to 0 for the next user restart and
the one-shot trace request was cleared. PID 26738 still uses threshold 300.
Keep gpu_profile=1, cube_profile=1, cube_early=1, executor=1, ui_native=0,
lazy_clear=0 and pass_barrier=0 fixed, with the same Camera settings. No new
APK is required. After the manual restart, verify a new PID/TID and actual
threshold 0 before selecting warmed, complete report windows. In this menu,
removing the one early submit need not reduce pass count; it tests how
submission overlap/gaps affect the elapsed queue measurement and frame pacing.
Do not interpret a shorter queue interval alone as less GPU work or cooling.
No kick=0 result or speedup is established yet.

The fresh sequence and baseline raw data are in the ignored directory
performance-results/2026-10-28/poco-submit-analysis/, with exact selection,
GPU/CPU summaries, screenshot and next-launch property metadata in kick300/.

## Installed kick=0 menu comparison

The user restarted into Camera. New PID 10873 / renderer TID 12593 /
executor TID 12590, GPU startup 15:12:15.805. Actual early-submit threshold
0 is logged at 15:12:16.409; no early submissions accumulate. Detailed/cube
timestamps and early reflection assembly remain enabled; UI and lazy clears
remain off. Saved options and TRACE environment match the kick=300 baseline
byte-for-byte and remain unchanged across the new capture. Start/end images
show the same Camera menu at 100% scale, with no obvious corruption.

Thirteen complete GPU5 intervals wholly within 15:14:28–15:15:40 contain
3900 completed frames. Screenshots, startup/navigation and the later PASSSEQ
request are outside that selection. Compare with the seven-window kick=300
baseline above; both APK runs use the same cube timestamp instrumentation.

| Metric | kick=300 | kick=0 |
| --- | ---: | ---: |
| Completed GPU5 frames | 2100 | 3900 |
| Game queue interval, ms | 8.8620 | 4.0053 |
| Final output queue interval, ms | 2.6797 | 2.5939 |
| Combined queue interval, ms | 11.5417 | 6.5992 |
| Rendering instances / full barriers / blits | 16 / 18 / 13 | 16 / 18 / 13 |
| Indexed draws / indices per frame | 624.69 / 65176 | 624.88 / 65197 |
| Depth instances / nominal LOAD and STORE Mpix, each | 11 / 1.872 | 11 / 1.872 |
| Reflection copy+mip interval, ms | 0.1761 | 0.1734 |
| Renderer flips/s, separate complete PERF5 windows | 59.9428 | 59.9426 |
| Renderer CPU, percent of one core | 67.56 | 67.42 |
| Queue submits per flip | 2 | 1 |
| Queue-submit host duration per flip, ms | 0.5566 | 0.3697 |
| Fence-wait host duration per flip, ms | 0.0471 | 0.0495 |

The combined elapsed queue interval is 4.9425 ms lower while the recorded
rendering workload and renderer FPS/CPU remain essentially unchanged. This
is consistent with the earlier interval including CPU-side recording and
scheduling gaps between the two submissions. Source inspection establishes
that early commands can execute before recording/submitting the tail, but
these samples do not directly measure that gap or isolate GPU busy time.
Do not report this as 4.94 ms less shader/attachment work, an FPS gain, or
reduced power/heating. It is one chronological two-process comparison with
uncontrolled clocks, not a repeated kick-mode benchmark.

GPUCUBE has one measured build per completed frame, six face copies, seven
mip blits and no missing/overflow groups. In the independently bounded
VKCUBE interval 15:14:23.732–15:15:38.806, frame 7500→12000, early builds
and uses both rise by 4500; lazy builds remain 1 and same-frame fallbacks 0.
This counter interval is not the exact 3900-frame GPU5 selection.

The fresh sequence at 15:15:48.815 / frame 12600 has the same 15 guest
instances plus final output. The main instance now ends at begin_rendering
before changing to the lighting target, rather than kick_submit. No
main-geometry pass was removed. The one-frame draw counts differ because
the car continues animating. The current diagnostic remains kick=0, allowing
the existing live pass-barrier experiment to be checked without another
startup or graphics change. Defaults have not changed.

Raw kick=0 data and exact GPU/CPU/cube summaries are in
poco-submit-analysis/kick0/; kick-comparison.json retains both selected runs.

## Live pass-barrier follow-up

In the same kick=0 process the existing scoped consumer barrier was tested
without changing graphics. The first original/scoped/original cycle gives
game intervals 4.0214 / 3.4587 / 4.0557 ms and combined intervals
6.6290 / 6.0584 / 6.6754 ms, with unchanged pass/blit/depth/total-barrier
counters and roughly 60 flips/s. This is a promising local queue-time effect.

A later repeat encounters global timing drift already in original mode,
wireless ADB loss/recovery and several presentation orientation rebuilds.
Its planned sampling window crosses a rebuild/lifecycle gap. The late data
and exploratory adjacent controls are retained separately, rather than
presenting this as a clean repeated benchmark. Details, stage times and
limitations are in [the pass-barrier report](vulkan-pass-barrier.md#poco-camera-follow-up-2026-10-28).

The original barrier is restored and confirmed live at 15:27:24.868. At
15:34:19 the next manual launch is prepared with gpu_profile=2 and cube_profile=0,
keeping kick=0 and cube_early=1. Current PID 10873 still runs startup profile=1
with cube timestamps until the user restarts it. A new APK is unnecessary;
the next confirmation removes detailed per-pass/cube timestamps, while
holding Camera foreground and orientation fixed. No shipping default changes
or heat/power improvement are established.

## Coarse-profile confirmation of the barrier candidate

After the user's restart, PID 12619 / renderer TID 21493 starts at
15:36:58.543 with mode=2, pass_timestamps=0 and cube_timestamps=0. The same
Camera settings, kick=0 and early cube assembly are retained. A complete
original/scoped/original/scoped sequence in this one process gives game
intervals 4.1520 / 3.4380 / 4.1196 / 3.4606 ms. Output remains
2.5819 / 2.5820 / 2.5996 / 2.5826 ms. All 22 selected complete windows,
6599 frames total, retain 16 passes, 13 blits and 18 total barriers; their
consumer scopes change rather than the barrier count. Geometry is comparable,
with about 624–637 draws and 65040–65314 indices per frame.

The combined interval falls by roughly 0.7 ms on both on segments and rises
again on return. Thus the local queue-time benefit persists without detailed
pass/cube timestamps. Unlike the previous repeat, this launch has only the
startup VKROT selection, no subsequent rebuilds, and unchanged PID/options/env.
FPS remains about 59.9 in all stages. This does not measure GPU busy, energy,
heat or a universal improvement; racing and profiling-disabled validation
remain pending. Exact intervals, counters and limits are in the
[coarse-profile report](vulkan-pass-barrier.md#poco-coarse-profile-confirmation-2026-10-28).

The original barrier is restored and confirmed at 15:45:12.463. Current
process/settings remain gpu_profile=2, cube_profile=0, kick=0, cube_early=1,
pass_barrier=0; no source default changes. The next race check can use this
process without an APK build/install or restart. Raw data is under
poco-submit-analysis/pass-barrier-coarse/ in the ignored results directory.

## Follow-up race check of the scoped barrier

The user entered racing in the same coarse-profile process. Actual switches
0→1→0→1 are confirmed, with unchanged graphics/options/environment and no
presentation rebuilds. During the first on segment the user reported normal
appearance, without flicker, missing objects or reflection/lighting errors.
Moving-race segments have different geometry and do not establish an FPS gain.

After the car stopped in one position, a separate exploratory original/scoped
pair gives game interval 6.0426→5.0038 ms, output 3.2534→3.2540 ms and about
198052→198063 indices per frame. Both run near 60 renderer flips/s with almost
unchanged renderer CPU. This supports the menu candidate without establishing
a general racing/thermal improvement; it is one close-workload pair with
uncontrolled clocks. Full data and limitations are in the
[race report](vulkan-pass-barrier.md#poco-race-check-2026-10-28).

For continued experimental play on this Poco, pass_barrier is left 1,
kick=0 and cube_early=1. At 15:59:30 next-launch gpu_profile is prepared as 0
(cube_profile already 0); the current PID still uses startup profile=2 until
the user's next restart. No new APK or source default change is made. Raw
race data is under performance-results/2026-10-28/poco-pass-barrier-race/.

Raw logs, scene screenshots, SurfaceFlinger snapshot, options/environment,
pass sequences and frame-weighted summaries are in the ignored directory
performance-results/2026-10-28/poco-gpu-menu/. The GPU summary helper is
performance-results/2026-10-27/asus-gpu-race/summarize.py; its --tid, --since and
--until filters retain only complete GPU5 report intervals.
