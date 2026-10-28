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

Raw logs, scene screenshots, SurfaceFlinger snapshot, options/environment,
pass sequences and frame-weighted summaries are in the ignored directory
performance-results/2026-10-28/poco-gpu-menu/. The GPU summary helper is
performance-results/2026-10-27/asus-gpu-race/summarize.py; its --tid, --since and
--until filters retain only complete GPU5 report intervals.
