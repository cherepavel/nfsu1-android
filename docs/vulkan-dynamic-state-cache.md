# Android dynamic-state command cache

The Camera menu records about 202 UI draws with 810 indices. Source inspection
found that Android `apply_dynamic` unconditionally emits four Vulkan commands
per draw: blend constants, stencil compare mask, stencil write mask and stencil
reference. Thus that UI phase alone requests approximately 808 state writes,
regardless of whether the effective values changed. At preparation, the number of repeats on
the actual device was not measured; the installed comparison is recorded below. These calls may already be cheap inside
the driver; fewer API commands do not guarantee less GPU time.

Existing investigations constrain the next experiment: compact fragment
specialization changed UI timing by only about 0.02 ms; unused-depth omission
did not improve the menu. Neither is enabled here. A fresh SurfaceFlinger
snapshot still shows the game layer as DEVICE with transform 0, so the old
redundant rotation composition is not present in that snapshot. Evidence is
`performance-results/2026-10-27/asus-ui-audit/surfaceflinger.txt`.

## Implementation

Caching is enabled by default on Android following the user's decision to
retain its small CPU benefit. Unset/empty and explicit 1 enable it; explicit
0 and invalid nonempty values keep all four setters on every state setup.
`RECOMP_VK_STATE_CACHE` overrides the Android property. Properties are polled
every 300 renderer flips, without per-draw property reads or clocks; live
switching needs no process restart.

Only identical effective values are suppressed. Blend constants compare the
full original packed color. Stencil compares the lower eight bits, exactly
what the existing calls pass to Vulkan. Both stencil faces are always updated
together. Changes to any single value emit that setter, and an invalid cache
emits all four, including zero values.

All guest pipelines declare these four states dynamic (the first six entries
of their dynamic-state list include viewport/scissor). The renderer's existing
`s_dyn_valid` invalidation covers new command buffers, frame reuse, ring flush,
early submit, and the static-state presentation pipeline. Live mode changes
also invalidate it. These lifetime constraints matter: binding a pipeline with
static state can invalidate earlier dynamic values, as illustrated in the
[Khronos dynamic-state lifetime guide](https://docs.vulkan.org/guide/latest/dynamic_state.html).
No draw, shader, texture, barrier, target, resolution or filtering is removed.

`[VKSTATE]` in performance.log reports the preceding interval:

- `cache`: mode used for those counters; `next_cache`: mode after this marker.
- `draws`: state-setup calls, not a new independent GPU draw counter.
- `requested`: four setters per setup, as in the original implementation.
- `emitted`: setters actually recorded; in mode 0 this equals requested.
- `redundant`: requested values already defined and unchanged in this command
  buffer, counted in both modes. This includes baseline commands that are
  actually emitted; mode 1 suppresses them.

The counters add comparisons/arithmetic to both modes. The baseline is the
new APK's mode 0, not a timing sample from an earlier APK. Stencil values are
cached even while stencil testing is disabled, so later stencil draws retain
the same previously defined values.

## Validation and next capture

`tests/vulkan_dynamic_state.c` includes the production selector against a mock
Vulkan command buffer. It checks initial zero states, repeat suppression,
independent changes, masked guest upper bits, baseline writes, cache
invalidation/new command buffers, and final driver state across 100,000 mixed
updates/invalidation/mode sequences. ASan/UBSan pass. Android compiler syntax
checks pass in both the original configuration and the user's register-block
+ executor-TLS-argument configuration; existing unused-symbol warnings remain.
This is not real GPU visual or performance validation. The user builds,
installs and starts the game; no full APK build/install was performed here.

Build with the established executor settings:

```sh
./gradlew assembleDebug -Pnfsu.registerBlock=true -Pnfsu.executorTlsArg=true
```

Hold `cube_early=0`, `vk_kick=0`, `gpu_profile=1` and graphics quality fixed.
Use the same Options → Camera scene. Start with state_cache=0, capture its
redundant counters and pass timing, then live-switch 0→1→0→1 in the same
process. Select whole stable windows after actual next_cache markers and
exclude transition windows. This switch does not compile new pipelines.
Require equal draws/indices/pass/blit/attachment counts and inspect image,
particularly blend fades and stencil clipping. Compare renderer CPU as well
as game/output queue intervals; this change targets command recording and may
have little or no GPU benefit. The preparation default was 0; the installed
comparison and subsequent decision below supersede it.

The prior 2.46 ms `present_ms` must be described as the final output queue
interval, not an isolated FXAA shader measurement: it includes transitions
and any image-readiness waits. No FXAA rewrite is justified by that number
alone. The actual shader already has a low-contrast early exit; quality and
sample locations are preserved in this experiment.

## ASUS installed same-process 0→1→0→1, 27 October 2026

The user built, installed and entered Camera. Game PID 3729 / renderer TID
3865 throughout. The four stages were switched live, without a process
restart, APK change or game input. Saved options are byte-identical: 250%,
FXAA/Square/reflections on, 16x anisotropy, 60 FPS, blur off. cube_early=0,
kick=0, detailed GPU profile=1 throughout. The existing CPU instrumentation
also remains on.

Each stage starts at an actual VKSTATE next_cache marker, excludes 15 seconds,
then selects whole report windows inside marker+15 through marker+45. There
are five GPU5 windows and 1500 frames in each stage. PERF5 and state summaries
also select only whole independent windows; their boundaries differ from GPU5.

| Stage | Mode marker MSK | Game ms | Output ms | Total ms | UI ms | Renderer CPU % one core | State setters/frame |
|---|---|---:|---:|---:|---:|---:|---:|
| A1 off | 02:13:00.512 | 12.2230 | 2.4584 | 14.6814 | 2.8994 | 40.53 | 2512.4 |
| B1 on | 02:13:50.560 | 12.2308 | 2.4584 | 14.6892 | 2.9008 | 39.97 | 4.0 |
| A2 off | 02:14:40.609 | 12.2342 | 2.4584 | 14.6926 | 2.8990 | 40.64 | 2508.0 |
| B2 on | 02:15:30.659 | 12.2308 | 2.4564 | 14.6872 | 2.9006 | 39.98 | 4.0 |

State setters fall by about 99.84%, to the four initial writes per frame.
The new counters therefore confirm execution of the cache, rather than a
property-only change. All four stages have 17 instances, 19 full barriers,
14 blits, 12 depth instances and 14.258 Mpix nominal depth LOAD/STORE each.
Guest draws/frame are 630.42 / 629.74 / 629.24 / 627.34; indices/frame are
65485 / 65378 / 65277 / 64925. UI is approximately 202 draws / 810 indices.
Renderer flips remain approximately 59.94/s in all stages.

Off means: total interval 14.6870 ms, UI 2.8992 ms, renderer CPU 40.5828%.
On means: total interval 14.6882 ms, UI 2.9007 ms, renderer CPU 39.9771%.
There is **no measurable GPU/UI interval improvement** in this test. Renderer
CPU decreases by about 0.606 percentage points of one core (about 1.5% relative
to its baseline CPU use); the direction repeats, but it is a small effect and
is not a GPU/thermal solution. Fewer API commands do not imply fewer GPU state
packets; internal driver deduplication is consistent with these results but
has not been directly observed.

Screens preserve the same animated Camera scene without obvious corruption.
A track-change banner is visible at the A2 endpoint. The last selected A2
GPU5 window contains about 0.4 extra UI draw/frame; preceding windows remain
202. Its first four windows alone give 14.6860 ms total, leaving the
no-GPU-benefit conclusion unchanged. The main comparison retains all five
preselected windows. No frozen-frame image comparison, temporal correctness,
race/stencil stress or controlled-frequency/power measurement was performed.
Battery snapshots range from 39.2 to 40.0 C; do not infer cooling from them.

The collector restored state_cache=0 and actual next_cache=0 was logged at
02:16:20.708. At that point the default remained off. cube_early=0, kick=0 and
profile=1 remain unchanged.

Raw per-stage logs/options/start-end screens, mode markers, reproducible
selection boundaries and summaries are in
performance-results/2026-10-27/asus-state-cache/. collect.py records/restores
the live sequence; analyze.py produces comparison.json plus separate
GPU/CPU/state summaries. The next useful audit should measure actual UI draw
coverage/overlap and compatible adjacent batches; removing these repeated
setters does not explain the roughly 2.9 ms UI interval.

## Retained at the user's request

The user subsequently asked to keep the cache and defer 2D investigation.
state_cache=1 was applied live, and cache=1 / next_cache=1 was confirmed.
The source default is enabled for subsequent user builds; explicit 0 rollback
remains available. The installed APK still uses its original default and is
kept enabled through the property. The measured result remains a small CPU
benefit with no measurable GPU reduction. No build or install was performed.
