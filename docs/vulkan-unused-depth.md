# Experimental omission of unused depth/stencil attachments

2026-10-21. The barrier experiment did not show a reproducible speedup, even
with kick=0. This next hypothesis tests attachment load/store work without
changing resolution, FPS, FXAA, anisotropy, reflections or shader calculations.

`target()` obtains a depth image whenever the guest supplies a zeta address;
`begin_rendering()` attaches it with LOAD/CLEAR and STORE. This happens even
when a draw has depth test, depth write and stencil test all disabled. The
code path is confirmed; its current menu coverage and driver cost are not.
[Android's GPU guidance](https://developer.android.com/games/optimize/optimization-tips)
identifies unused attachments as possible bandwidth/resource costs. It does
not establish that this driver executes all nominal loads/stores.

## Conservative selection

Android `debug.nfsu.unused_depth` selects 0 (original), 1 (broad diagnostic)
or 2 (restricted to recent screen-output targets). Unset/invalid selects 0.
Mode 1 caused a measured regression; use mode 2 for the next experiment.
Polling occurs on draws every 300 flips; performance.log's
`[VKDEPTH] omit_unused=N frame=... screen_only=M` records actual mode changes.
Default is 0.

The draw can omit its requested depth image only when:

- Guest depth test (`0x30C`), depth write (`0x35C`) and stencil test (`0x32C`)
  are all exactly zero. Nonzero/unknown values retain the image. Checking depth
  write separately is deliberately stricter than Vulkan requires when testing
  is disabled. Depth bounds testing is not enabled by this renderer.
- The depth image has no pending clear, and its stored dimensions exactly
  match color. Thus omitting it does not enlarge the render area.
- It is not already attached to a continuing compatible pass. An existing
  depth pass is kept to avoid forcing an immediate store/reload. A pending
  color clear already requires a new pass, so that case may omit depth.

Mode 2 additionally requires the color surface to have been selected as a flip
output within four renderer flips. Each surface records that observation;
reallocation clears it. This covers alternating backbuffers without naming
guest addresses and expires after a scene repurposes a target. The counter's
unsigned age handles wraparound. Being a flip source is a performance guard,
not a guarantee about future depth use, and does not assert actual scanout.

All clear commands retain the original attachment and clear logic. Images are
neither dropped nor invalidated; subsequent depth/stencil-using draws attach
the same image with the original LOAD behavior. Barriers/layouts stay unchanged.
No unused shader/color draw is removed. The
[Vulkan pipeline state definition](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineDepthStencilStateCreateInfo.html)
and [fragment operations](https://docs.vulkan.org/spec/latest/chapters/fragops.html)
describe the disabled depth/stencil operations on which this guard relies.

Selection is checked before deciding whether an early submit is mid-pass, then
recomputed after any early-submit/ring-overflow pass end. The selected image
controls both PipeKey.depth (Android's compatible render pass/pipeline) and
`begin_rendering`/dynamic state. Instanced commands retain their existing
pipeline/attachment until the usual pending-draw flush.

If later draws in a color-only pass enable depth/stencil, reattaching it needs
a new pass. Consequently **pass count can increase**; that would be a real
tradeoff and must be measured. This code does not know the future guest stream.
Keeping an already-open depth pass can also leave much of the theoretical
opportunity untouched. Do not assume that all UI draws become color-only.

## Recorded-work counters

Both GPU profiling modes add per-frame means to GPU5:

- `depth_passes`: guest rendering instances with a depth/stencil attachment.
- `depth_load_mpix`: millions of render-area pixels with depth/stencil LOAD.
  Pending full clears use CLEAR and are excluded from this load count.
- `depth_store_mpix`: millions of render-area pixels with depth/stencil STORE.

They count recording policy, not hardware memory transactions, compression or
GB/s. A driver may eliminate some work already. Color load/store policies
remain unchanged. In detailed profiling, GPUPASS grouping and PASSSEQ also
include `depth=0/1`, identifying which target/phase actually lost an attachment.
The final FXAA output has no depth and contributes none of these depth counters.

## Next manual-install test

Build from the root with `./gradlew assembleDebug lintDebug`. The user signs
and installs the existing `android/app/build/outputs/apk/debug/app-debug.apk`;
the agent does not install/uninstall or change the package/path.

Next-launch properties have been prepared as profile=2, kick=0, pass_barrier=0,
unused_depth=0. Alpha/zero-source/framebuffer cache/native YUV/fence wait stay
on; compact shader/deferred clears stay off. This diagnostic kick policy is
for both sides of the comparison, not a new shipping default.

After installation, fully restart and open the same Options → Camera scene:
250%, 60 FPS, FXAA, Square Pixels, reflections and 16x anisotropy on;
Blur/Bloom off. Wait 30 seconds. Verify a new PID/TID, mode=2, threshold 0,
VKDEPTH mode 0 and the current options. Capture separate logs for the new launch.

Compare unused_depth **0→2→0→2** in that same process, at least 60 seconds per
stage after each actual VKDEPTH marker. Exclude report ends in the first 15
seconds after each marker and scene warmup. First activation can compile new
depthless pipelines: exclude compilation/warmup and inspect the return/repeat.
The live property is `debug.nfsu.unused_depth`; pass_barrier must stay 0.

Require a real fall in depth attachment/load/store counters, comparable guest
draw/instance indices and blits, and repeatable game+present interval reduction.
Report any extra passes/barriers and evaluate their total cost. A counter
reduction alone does not establish an optimization. Check image appearance,
depth ordering, fog, reflections, UI and text with the user. Animate/rotate the
car; later racing and lifecycle checks are needed before enabling by default.

```sh
python3 tools/summarize-pass-barrier.py CAPTURED_PERFORMANCE_LOG \
  --experiment unused-depth --tid RENDERER_TID \
  --since LAUNCH_ISO_TIME --sample-after STABLE_CAMERA_ISO_TIME
```

The summary selects VKDEPTH markers and includes the depth counters. It keeps
each return/repeat separate and does not infer scene stability. Save raw logs,
images and thermal/SF snapshots in the ignored 2026-10-21 results directory.
At completion restore unused_depth=0/pass_barrier=0, kick=300/profile=0; the
last two take effect only in the next process. Validate any promising result
with timestamps disabled and the production kick policy.

## Validation before installation

- `tests/vulkan_unused_depth.c`: ASan/UBSan, host and simulated Android. Covers
  every enabled-state combination, non-Boolean enable bits, pending depth
  clears, unequal extents, continuing depth/color-only passes, target switches,
  submit/flush reselection, later reuse of the original image and live rollback.
- `tests/vulkan_deferred_clear.c`: both policies pass ASan/UBSan, including
  shared/partial clear behavior, depth load/store counters, and color-only →
  depth → unused transitions through production rendering helpers. A continuing
  pass is retained, and depth is reattached with LOAD rather than discarded.
- Summary smoke checks pass for old barrier logs and independent VKDEPTH mode
  markers, warmup filtering and depth counter weighting.
- `./gradlew assembleDebug lintDebug` passes. No live correctness/performance
  claim yet; menu coverage and driver behavior await manual installation.

## Broad mode 1: controlled menu result

PID 31018 / renderer TID 31143, 2026-10-21 12:07:26 MSK. Startup confirms
profile=2, kick=0, both shader specializations on, compact/deferred clear off.
Saved options and screenshots confirm the requested Camera scene and quality.
Samples start after 12:08:05; the preceding loading/mixed-scene windows are
excluded. Actual mode markers and the 15-second exclusion define the later
segments. The values below are frame weighted:

| Mode | Windows | Frames | Game ms | Output ms | Total ms | Passes | Depth passes | Depth LOAD/STORE Mpix |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 22 | 6600 | 11.7988 | 2.1411 | 13.9399 | 17 | 12 | 14.258 |
| 1 | 31 | 9299 | 11.8872 | 2.2548 | 14.1420 | 18 | 11 | 11.698 |
| 0 return | 26 | 7799 | 11.7996 | 2.1410 | 13.9406 | 17 | 12 | 14.258 |
| 1 repeat | 15 | 4500 | 11.8942 | 2.2573 | 14.1515 | 18 | 11 | 11.698 |

Both on segments are ~0.20–0.21 ms (~1.5%) **slower** despite ~18% less nominal
depth load/store area. Global barriers grow 19→20; blits stay 14. Guest indexed
draws average 629.0–630.6/frame, indices 65,158–65,240/frame. The user reported
no visual/pacing change, and screenshots show no obvious corruption. Live
animation does not establish pixel equivalence. Mode 1 stays disabled.

The four short SurfaceFlinger snapshots contain only 2/20/53/32 valid presents;
the return-A sample has one 33.10 ms gap, while the other samples have none
over 25 ms. These sparse buffers do not establish a pacing difference. GPU0
HAL readings at B1/A2/B2 were 59.2/60.3/59.6 °C; frequency and thermal state
were not fixed, so this is not a measured energy/temperature saving.

Raw `unused-depth-*` logs, screenshots, state/SF snapshots and summary JSON
are in the ignored 2026-10-21 results folder. The summary command uses renderer
31143, since 12:07:26, sample-after 12:08:00, experiment unused-depth.

## Pass map explains the tradeoff and motivates mode 2

The installed APK was restarted with profile=1/kick=0 for **structural** traces;
PID 4407 / renderer 4718 started at 12:16:47. The agent dismissed the startup
profile prompt with Back and restored Options → Camera without changing the
saved graphics settings. Tokens `ud-a` and `ud-b` captured frames 6900 and
10800 at 12:18:44.784 and 12:19:49.847. Timing from this process is not mixed
with the coarse A/B above. Guest pass counts exclude final FXAA.

| Target/phase | Mode 0 | Broad mode 1 |
| --- | --- | --- |
| 82A4ED80, 2133×1200 light target | one depth pass, 7 draws / 30 indices | color-only pass, 1 draw / 6 indices, followed by depth pass, 6 draws / 24 indices |
| 83240000 final UI, 2133×1200 | depth attached, 203 draws / 814 indices | depth absent, 203 draws / 814 indices |

The remaining target/depth sequence is unchanged; 3D geometry counts vary with
animation. This identifies the added split on the light target and the unused
attachment on the complete UI phase. It does not isolate hardware bandwidth
or prove that the added pass alone explains the entire measured regression.

Mode 2 retains the offscreen light attachment from its first draw and allows
omission on recently selected flip targets. The tested menu should retain the
color-only UI phase without the light-target split. Expected recording policy:
17 total passes, 19 full barriers, 11 depth passes and 11.698 Mpix LOAD/STORE.
These are **predictions pending the new APK**, not live results. Other scenes
can still re-enable depth later, so count and measure any additional passes.

Tests now cover recent/expired/unseen screen targets, unsigned frame wrap,
independent surfaces, mode-2 property polling, and an offscreen disabled-depth
prefix followed by a depth-using draw remaining in one pass. Both ASan/UBSan
policies pass. The broad mode is retained as a diagnostic control; no mode is
enabled by default.

The mode-2 APK passed `./gradlew assembleDebug lintDebug`; SHA-256 before user
signing is `d4d3419785eeb1e7a4a1da280fb560c78920ac8c3fce516fba60b67a5e926877`.
Next-launch properties are staged at profile=2, kick=0, unused_depth=0,
pass_barrier=0, trace_target=0. The current diagnostic PID 4407 still runs the
preceding APK with startup profile=1/kick=0; its broad mode is confirmed off
at 12:20:24.883. The agent has not installed the new APK. Build and staging
records use `unused-depth-screen-*` in the ignored results directory.

## Mode 2 partial live run and change of investigation

PID 10137 / renderer 10274, start 12:27:21 MSK, profile=2/kick=0 and unchanged
quality. The 0→2→0 sequence was stopped when the user redirected investigation
toward command management; the second B was **not performed**. Joined current
and rotated performance logs are required: rotation occurred during this run.

| Mode | Windows | Frames | Game ms | Output ms | Total ms | Passes | Depth LOAD/STORE Mpix |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 24 | 7199 | 11.8042 | 2.1372 | 13.9414 | 17 | 14.258 |
| 2 | 29 | 8699 | 11.8877 | 2.2572 | 14.1449 | 17 | 11.698 |
| 0 return | 10 | 3000 | 11.8312 | 2.1428 | 13.9740 | 17 | 14.258 |

Mode 2 preserved 17 passes and 19 full barriers, eliminating mode 1's added
pass, but this partial run showed no speedup. It remains disabled. Summary
uses since 12:27:21, sample-after 12:31:25, until 12:37:15. Later CPU profiling
is not included. This is neither a completed 0→2→0→2 nor a thermal comparison.
All depth/barrier experiments were switched off, and a clean normal-policy
process was launched for the [command-path audit](android-command-path-audit.md).
