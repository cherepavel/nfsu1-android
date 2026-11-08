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

## Mali-G615 race and measured hash overhead, 26 October 2026

Same installed vertex-setup APK, build ID
`e150b0e66d6108a71ef6a179f2a7007dcf006920`. Startup confirmed dynamic=1,
limit=32, native_push=0. PID 4059 / renderer 25211. User opened a race;
a screenshot confirms racing output. The 10:56:14–10:56:45 MSK capture
(cpu-clock, 1000 Hz, DWARF) has 23,861 samples, lost=0.

The three complete 300-frame cache summaries inside this recording show
663386/150764, 535819/132514 and 431681/112753 hits/misses: approximately
80.4% combined hits. Cache reuse is real. Driver allocation/update/bind
subtrees shrink from about 8.80% of renderer samples in the preceding
uncached race to 0.78%/1.70%/1.29% (3.77% combined). But the complete
`android_dynamic_descriptors` path costs **10.50%**, versus **9.47%** for
`android_push_descriptors` previously. Scenes/clocks differ; this is not a
controlled speedup/regression measurement. No net descriptor win is established.

DWARF attribution of self samples reveals a concrete application-side cost:
824 samples at the bytewise hash loop, plus nearby samples in the same loop
and probing (170 at the probe line). This is evidence for changing the hash,
unlike speculative tuning based only on reduced allocation counts.

The hash now processes 32-bit words via memcpy, avoiding alignment and aliasing
assumptions. Canonical zero initialization is retained; a compile-time size
assert prevents a tail overread after future key changes. High bits are mixed
before the table mask to handle aligned buffer/view identities. The table
still checks full-key equality and linearly probes collisions. No resource
identity, offset, range, reset or lifetime rule changes.

Validation after this change:

- Existing host ASan/UBSan regression passes: effective addresses/ranges,
  resource identity, reversed write order, separate frames, reset, collisions
  and full-cache behavior.
- Production helper GPU test on Mali: 1024 offset triples / 4096 uint components,
  zero mismatches against ordinary sets and CPU; 1021 hits / 3 misses.
- Isolated on-device cache-lookup benchmark, 512 synthetic canonical keys,
  one million successful lookups per pass, four alternating pairs. Bytewise:
  572.731/572.939/572.523/572.465 ms thread CPU. Word hash:
  149.060/148.656/148.569/148.654 ms. All checksums 256499232.
  This includes probing/comparison but no Vulkan calls; different keys and
  table occupancy can change the result. It is not an FPS measurement.
- `./gradlew assembleDebug lintDebug`: BUILD SUCCESSFUL; diff check passes.

Candidate APK SHA256:
`a0d813eac6ed1e6bd381f413a0070473e242e6f655014b0375d104648940d959`.
It is built at the usual debug APK path and has not been installed. The old
cache experiment was disabled with dynamic_desc=0 and the installed app
restarted after capture. To test the new APK, install it, set
`debug.nfsu.dynamic_desc=1`, restart and open the same race. Repeat/reverse
against 0 before enabling by default. Neither the current profile nor the
hash microbenchmark establishes a whole-renderer or FPS improvement.

Raw data and benchmark source/output are local ignored files under
`performance-results/2026-10-26/mali-descriptors/`.

## Word-hash installed off/on comparison

26 October, same APK `0661e800fcd1125ab6537a6c6780d6da1386260d`:
control PID 21887 / TID 29264, 11:05:17–11:05:38 MSK; cached PID 29416 /
TID 11914, 11:07:17–11:07:38. Both request 20 seconds of cpu-clock at 1000 Hz.
Control 18,334 samples, cached 13,530 samples, both lost=0. Cached running
samples end roughly 18 seconds after the first; use actual sampled range for
window selection, not the nominal requested duration. Screenshot confirms race.

Complete descriptor path inclusive share: **9.30% uncached / 8.07% cached**.
This is 1.23 percentage points less, about 13.2% relative to descriptor share.
The word-hash cached share is also below the earlier byte-hash cached 10.50%,
but those recordings have different scenes and cannot establish exact savings.
Allocation/update/bind driver shares in the word-hash run are about
0.87%/1.54%/1.30%. Resource correctness tests remain as documented above.

Three complete PERF5 windows within each sampled range:

| Metric | Off | On |
|---|---:|---:|
| Window sum, s | 15.0105 | 15.0074 |
| Renderer CPU, s | 13.4773 | 11.6870 |
| Input draws | 1522970 | 1315232 |
| Flips | 668 | 487 |
| Flips/s | 44.50 | 32.45 |
| Draws/flip | 2280 | 2701 |
| Renderer CPU/input draw, us | 8.85 | 8.89 |

The workloads differ materially. Whole-renderer CPU/draw is essentially
unchanged, and the lower observed flips/s must not be attributed to the cache
from this pair. No overall FPS or CPU improvement is established. The cache
remains opt-in; no production default was changed. Current phone process
retains dynamic_desc=1 for the experiment; it was not interrupted again after
capture. Raw captures are `mali-descriptors/word-{off,on}/` locally.
