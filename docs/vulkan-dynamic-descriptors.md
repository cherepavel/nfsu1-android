# Per-frame descriptor reuse with dynamic uniform offsets

2026-10-21. Opt-in experiment following the negative native-push A/B/A/B result
in [the command-path audit](android-command-path-audit.md). No in-game speed,
GPU-load, power or thermal improvement is claimed yet.

## Hypothesis and implementation

The established CPU hotspot is allocating/updating a complete set for each
guest draw. Switching each call to native push descriptors made it more
expensive on this Adreno driver. Instead, reuse descriptor sets when their
textures and uniform ranges agree, passing the three changing ring offsets
through `vkCmdBindDescriptorSets` dynamic offsets. This reduces the operation
count itself if the scene repeats texture combinations.

Startup-only `debug.nfsu.dynamic_desc=1` selects this path when
`maxDescriptorSetUniformBuffersDynamic >= 3`. Default/unset/0 preserves the
old implementation. It takes precedence over native_push, so the two layout
types cannot be mixed. `[VKDYN] requested=… dynamic_limit=… active=…` confirms
the startup selection; restart is mandatory. Bindings 0, 1 and 6 become dynamic
UBOs, with offsets passed in increasing binding order; the shader interface and
bytes read are unchanged. The final FXAA sampler uses its existing set path.

The key contains all three buffer handles and exact ranges, plus all four
samplers, image views and image layouts. Offset is normalized to zero in the
stored UBO descriptors. The original aligned offset is supplied dynamically.
Binding 6's range can shrink near the ring end and is part of the key. No
VK_WHOLE_SIZE is used. The helper validates alignment, 32-bit offsets, explicit
nonzero ranges, and bounds within the existing 48 MiB ring. See
[Khronos dynamic offsets](https://docs.vulkan.org/guide/latest/descriptor_dynamic_offset.html)
and [descriptor-set requirements](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html).

Each of the two Frame objects owns a separate CPU index, at most 4096 entries
with an 8192-slot hash table (about 0.72 MiB per active frame). A cache hit only
binds an existing immutable set. A miss allocates and updates a new one; an
index at capacity continues allocating correct uncached sets. It never evicts
or updates a set referenced by an earlier draw. Index and descriptor pool reset
together in `android_frame_reset`, after the frame fence and before retired
images are destroyed. Resources, views and ring generations are never reused
through this cache across frame resets. There is no shader, texture, quality,
render-target, pass, draw-order, barrier, early-submit or fence change.

`[VKDYN] frame=… hits=… misses=…` reports guest-binding operations once every
300 flips; no clocks or property queries occur per draw. FXAA is excluded from
these counters. Counts are evidence of mechanism, not evidence of speed.

## Validation before installation

- `tests/vulkan_dynamic_descriptors.c` passes host ASan/UBSan. It checks that
  changing offsets hits the same set while preserving effective addresses and
  exact ranges; buffer/view/sampler/layout/range changes miss; reversed input
  write order preserves binding order; the two frames stay separate; reset,
  hash collisions, a full index, and FXAA delegation remain correct.
- `tests/vulkan_dynamic_descriptors_gpu.c` and `.comp` run headlessly on the
  Adreno 650 through the production key/cache/bind helper. A compute shader
  reads three UBOs at **1024 different offset triples**, including three range
  combinations. Ordinary per-dispatch sets, cached dynamic sets and CPU address
  calculations agree for **4096 uint components: zero mismatches**. Dynamic
  mode records **1021 hits / 3 misses**, versus 1024 ordinary allocations.
  This validates real driver buffer addressing and reuse, not rasterized image
  equivalence or performance in the game. The small test buffer and compute
  stage differ from the game's ring and graphics stages.
- `./gradlew assembleDebug lintDebug` passes. The APK remains at
  `android/app/build/outputs/apk/debug/app-debug.apk`; signing/installing is
  manual. No installation or package/path change is performed by the agent.

## Next live comparison

Install the newly built APK manually, open Options → Camera at the saved full
quality, and keep native_push=0, perf=0, gpu_profile=0, vk_kick=300. Start with
dynamic_desc=0, then restart 1 → 0 → 1 with the same APK and scene. Check both
startup markers and actual descriptor hit/miss counters. Capture renderer-only
stat/callchains sequentially and normalize by observed frame cadence where
available. Compare the entire renderer cost, including hashing, dynamic binds
and the driver, rather than just the removed allocation subtree. Confirm output
visually and exercise scene/resource changes before considering a default.
Only after a repeatable CPU win assess GPU queue and thermal behavior separately.

Ignored build log and headless probe output are under
`performance-results/2026-10-21/asus-menu/dynamic-desc-*`.

## Installed 0 → 1 → 0 → 1 result: no repeatable win

2026-10-21, same manually installed APK, warmed Camera with the full saved
quality and Game Genie HUD. Startup selection was checked for each process;
native_push/perf/gpu_profile=0, kick=300, attachment/barrier experiments off.
The user reported no visible change in the cached mode. CPU stat and callchain
recording were sequential, 15 seconds each, with no lost callchain samples.

| Mode | PID / renderer TID | Stat start MSK | Instructions, billion | Cycles, billion | Task-clock, seconds |
| --- | --- | --- | ---: | ---: | ---: |
| A1 / 0 | 14854 / 14916 | 13:40:06 | 13.858 | 7.312 | 6.918 |
| B1 / 1 | 16428 / 16485 | 13:43:05 | 13.780 | 7.274 | 6.880 |
| A2 / 0 | 17863 / 17919 | 13:45:33 | 13.816 | 7.310 | 6.911 |
| B2 / 1 | 19533 / 19590 | 13:48:59 | 13.813 | 7.351 | 6.931 |

Effective CPU frequency is close, 1.054–1.058 GHz. B1 cycles/second change by
−0.52%, B2 by +0.55%; instructions by −0.57% / −0.03%. This is not a repeatable
renderer improvement. Hit counters show **83.76%** reuse in both cached runs,
roughly 100 misses rather than about 630 allocations per frame. The mechanism
works but does not establish a useful speedup.

Renderer callchains explain the limit: old descriptor binding is 15.47% / 14.51%
inclusive, the cached helper 15.73% / 14.73%. Allocations shrink, but dynamic
`vkCmdBindDescriptorSets` grows from 1.41% / 1.91% to 7.82% / 7.03%, plus CPU
key/hash work. These are overlapping sampled cycle shares, not additive costs
or GPU timings. Do not optimize this cache further speculatively or enable it
by default on the basis of reduced API calls.

SurfaceFlinger TimeStats was enabled consistently across these four runs.
The game layer supplies 906–909 frames over 15.160–15.195 second SF windows,
approximately **59.76–59.82 FPS**. The SF window slightly exceeds the stat
interval, so these counts are not exact per-frame CPU denominators. SF's
reported `averageFPS` of about 62 uses quantized histogram bins and should
not replace the frame-count/window calculation. `droppedFrames=0` in these
windows. All 909–911 display compositions per window use client composition,
with reported `averageRenderEngineTiming` 1.605–1.656 ms. This is a separate
system metric and must not be added to game timestamps as pure GPU work.

At 13:49:48 dynamic_desc was restored to 0 and the app restarted, PID 20905.
The matching unstripped library was preserved as
`dynamic-desc-libnfsu2_recomp.so` before the next build. Raw captures/reports,
process markers and normalized results are in `dynamic-desc-*`, including
`dynamic-desc-abab-summary.json`. The investigation now follows the separate
[presentation/rotation composition path](vulkan-presentation-prerotation.md).
