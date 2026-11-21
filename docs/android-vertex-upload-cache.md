# Android vertex upload reuse

12 November 2026. First stage of the native-path investigation, branched from
`main` at `d931aee` as `codex/research-draw-geometry`.

The first candidate targets repeated vertex uploads in `upload_vertices`.
The existing [race profile](android-vertex-setup.md#installed-race-profile-105142105213-msk)
attributes 24.92% of renderer CPU samples to Xbox command/geometry preparation,
and 2.78% to Mali under vertex binding. Copying vertices inside the Vulkan
backend belongs to the separate Vulkan category. These measurements identify
areas to investigate, not this change's expected saving. The installed check
below confirms fewer copies/bindings; an isolated FPS/CPU/power improvement
has not been measured. Reuse is now enabled by default on Android at the
user's request after their gameplay check.

## Candidate and lifetime

Previously every draw gathered its direct vertex spans, copied them to the
frame's upload ring, uploaded constant attributes and bound all 16 vertex
buffers. The candidate keeps 64 direct-mapped upload entries. A full key
comparison, rather than the bucket hash, decides reuse. The key includes vertex
count, direct attribute mask, constant-attribute generation and every active
attribute's source pointer, type, size and stride. Textures, shaders, matrices,
indices and render targets are deliberately independent of vertex uploads.

Only uploads with no converted attributes, a nonzero constant generation and
at least 1024 bytes of aligned direct spans qualify. Unsupported GPU formats,
executor-converted attributes, small uploads and constant-only draws follow
the existing code. Indices and uniforms still upload normally. Draw order,
draw count, primitive topology, graphics settings and descriptor policy remain
the same; instancing is not enabled by this change.

Entries are valid only for the same guest `vtx_epoch`, upload-ring generation
and ring buffer. `Nv2aRawBatch` already supplies the epoch/pointer immutability
contract used by instancing. Semaphore release and guest NOP traps advance the
epoch; inline arrays advance it before and after each batch. Frame start and
mid-frame ring recycling change the ring generation. An entry also requires
its allocation end to remain within the current ring's allocated prefix.

A hit restores the original strides/formats for Android pipeline lookup and
uses the cached offsets. It skips vertex binding only when that entry is
still the last bound upload in the same command-buffer generation. An early
submit keeps ring data reusable but requires a fresh binding. Any intervening
ordinary or converted upload invalidates the last-bound identity. Cache misses
use the original pack/copy/bind path; insertion checks ring identity again
because allocation itself can flush the ring. No Vulkan objects are allocated
for this cache; its fixed metadata is renderer-owned and the existing frame
fences continue to govern upload storage.

The conservative draw-level ring reservation is retained. Near ring exhaustion
it can recycle the ring before a hypothetical reuse hit. This stage does not
attempt to avoid that reservation, share data across frames, cache converted
attributes, or cache executor index rebasing.

## Modes and counters

Android reads `debug.nfsu.vertex_cache` once per process:

| Value | Behavior |
| --- | --- |
| unset, empty or `1` | Reuse qualifying vertex uploads and redundant bindings; default |
| `0` or invalid nonempty value | Original uploads |
| `2` | Measure matching uploads, retaining original copies and bindings |

`RECOMP_VK_VERTEX_CACHE`, if present in `nfsu1x_env.txt`, takes precedence,
including an empty or invalid value. Restart the game after changing the mode.
Non-Android rendering uses its existing path. The build retains the usual
register-block and executor-argument flags:

```sh
./build_debug.sh
```

Startup confirms `[VKVTX] mode=N`. In modes 1/2, each 300 flips reports interval
counts: upload `draws`, `eligible`, key `hits`, actual `copied_bytes`, matching
`reusable_bytes`, actually avoided `reused_bytes`, actual `binds` and
`skipped_binds`. Counters reset after each report. Bytes describe vertex spans
and constant attributes, excluding ring padding, indices and uniforms. The
hit threshold includes aligned span size. These counters have no per-draw
clock calls and do not measure CPU duration or GPU execution time. Mode 0 does
not collect the counters. Mode 2 itself adds lookup/bookkeeping overhead and
its additional original allocations can recycle the ring sooner than mode 1.

## Validation and device comparison

`python3 tests/vulkan_vertex_cache.py --sanitize` compiles the actual production
range packing, format handling, upload function and cache against a mock Vulkan
recorder. It compares bytes fetched through all 16 bindings and pipeline input
formats/strides with mode 0, including interleaved/unaligned/separate spans,
constant changes, unsupported/converted inputs, early submissions, guest epoch
changes, ring reuse/overflow/allocation failure, cache collisions and mode 2.
It passes host AddressSanitizer and UndefinedBehaviorSanitizer. Android
`assembleDebug lintDebug` with both established executor flags also passes.
The mock does not submit GPU work or prove whole-game image equivalence.
Separate processes also exercise the actual production startup selector for
default-on, empty values, explicit rollback, measurement mode, invalid values
and environment-over-property precedence.

No Android device was connected during the initial implementation. The
initial opt-in candidate at
`android/app/build/outputs/apk/debug/app-debug.apk` was subsequently installed
and profiled as recorded below.

Initial opt-in candidate APK SHA256 (before the default-on revision):
`af0b6a098245ee3df6051908dedfe51f31c3dd9e668e93f4699f561bbb4043cc`.

For example, before fully restarting the game:

```sh
adb shell setprop debug.nfsu.vertex_cache 2  # measure matching uploads
# For the subsequent comparison, select 1 (reuse) or 0 (original) and restart.
```

The originally proposed comparison used the same APK/menu/settings with modes
0 → 1 → 0 → 1 and a process restart for each mode. The user subsequently chose
to skip the disabled comparison, reported that gameplay felt better, and
requested enabling reuse by default and merging into `develop`. That choice
does not turn operation-count reductions into an isolated CPU/FPS measurement.
Mode 0 remains available for rollback; no further off/on test is pending.

## First installed Poco check, 12 November 2026

The user installed/launched the candidate, selected mode 1, and reported two
completed laps and a third in progress without visual artifacts. Startup at
21:44:07 MSK confirms mode 1 on Poco 24095PCADG / Mali-G615 MC2, game PID
29928 / renderer TID 32647. This is an initial gameplay smoke check, not
exhaustive image, loading or lifecycle validation. This capture predates the
default-on revision and used the explicit property override.

A 20-second renderer cpu-clock/1000 Hz DWARF recording at approximately
21:49:10–21:49:31 MSK contains 14,000 samples with zero losses. The recorded
ELF build ID `2c6256dc5d73f01500adb7336b862d27a5d25b52` matches the local
unstripped library. APK/process, quality and properties were not changed by
profiling; root mode was disabled. Quality is scale 100%, reflections/FXAA/
square on, 16x anisotropy, 60 FPS target and blur off. CPU counters and detailed
diagnostics are unset, GPU profiling is 0; default selectors leave them off.
The screenshot was collected after recording and shows the race, not a
pixel-equivalence comparison or proof of motion throughout the recording.

Three complete 300-frame counter windows, 21:49:12.475–21:49:27.490 MSK,
contain 900 renderer flips and 1,459,436 vertex-upload draw inputs:

| Counter | Observed |
| --- | ---: |
| Eligible uploads | 452,700 |
| Cache hits | 141,910 (31.35% of eligible) |
| Actually copied vertex payload | 2,737,657,088 bytes |
| Avoided vertex payload copies | 450,017,536 bytes (14.12% of copied + avoided) |
| Avoided vertex-binding commands | 91,319 (6.26% of upload draw inputs) |

These are operation/byte counters from one enabled run, not an off/on CPU
comparison or physical memory-traffic measurement. The selected windows were
bounded using approximate host recording times and device log timestamps;
precise host/device clock synchronization was not performed. They span 15.015
seconds and about 59.94 internal flips/s, not independently measured Android
presentation FPS. The CPU profile samples about 14 seconds of renderer CPU
over a roughly 20-second recording. No isolated CPU/FPS/thermal saving is
established; the user accepted this initial gameplay evidence and declined
the proposed disabled comparison.

Raw recording, matching symbols, options/properties, logs, post-record screen,
CPU attribution and selected counter windows are in ignored
`performance-results/2026-11-12/vertex-cache/race-on/`.
