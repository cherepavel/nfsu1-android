# Android uniform block reuse

13 November 2026. Branch `codex/uniform-block-reuse`, based on `develop`
at `b59e4b4` (small texture sample snapshots included).

## Fresh baseline

Poco 24095PCADG / Mali-G615 MC2, active race confirmed by screenshots.
The installed APK and baseline APK have matching SHA-256
`12ecba545d125922017d5ed74858d53704bddbca7a09e2ef23630226b2cc659d`;
the saved unstripped library matches native build ID
`faee28375371e04fed6f62fa9c799614d9913572`.
Simpleperf recorded the renderer thread for 20 seconds at 1000 Hz:
15,950 CPU-clock samples, zero lost samples.

`vk_draw_raw_impl` has 11.77% renderer self samples; `tex_get` 10.35%,
memcpy 6.73%, `nv2a_pb_exec_method` 5.03% and scan 4.94%.
`android_push_descriptors` has 9.62% inclusive samples, including driver
work. Its descriptor-allocation reuse is already active: complete 300-flip
reports in the capture show requests reused with zero new allocations.
These percentages overlap where inclusive counts are used. Uniform setup
is only part of raw draw work; this change does not target all of its 11.77%.

The baseline library and pre-change source were saved before rebuilding.
Raw profile, symbols, source attribution, logs and screenshots are under
the ignored `performance-results/2026-11-13/uniform-blocks/baseline/`.
There is no post-change game profile yet.

## Change and correctness

Previously every draw rebuilt and uploaded a 112-byte `VsBlock` and
384-byte `FsBlock`. Android now retains one immutable uploaded range per
stage and compares the complete inputs before rebuilding that stage:

- VS: logical/physical surface dimensions, effective depth range, all matrix
  and viewport-offset bits, both AA-scale bits, effective transform mode.
- FS: all 18 ARGB constants, fog color, effective alpha function and reference,
  all eight texture-scale bits.
- Both include native FPCR on ARM64 (rounding, flush and default-NaN controls).
  Other architectures use `fegetround()` for the host checks.

Keys use integer fields and raw float bits, with size assertions excluding
padding. Equality compares the complete key, without a hash or collision
assumption. Some equivalent inputs deliberately miss (for example different
nonpositive AA scales). The builders retain the original arithmetic and
initialize all existing std140 bytes, including FS padding. FPSR exception
flags are not inputs to shader values and are not retained in the key.

A hit also requires the same ring generation, buffer identity and a range
ending at or before the current ring offset. Frame begin and synchronous
ring rewind advance the existing generation after the necessary fence wait.
An early submission changes the command buffer but preserves ring contents,
so it can reuse the same immutable range. Hits never rewrite mapped data.
The existing conservative whole-draw ring reservation is retained, including
space for both blocks, to prevent a mid-draw flush under earlier bindings.
Failed allocations do not publish a new cache entry.

Each stage backs off independently after four consecutive probed misses:
the next eight draws build/upload that block without making a key. Continued
misses then probe once per nine draws; a hit resets the miss streak. A new
ring scope resets the backoff. Skipped uploads leave the previous immutable
entry intact. When both stages are cooling down, a combined builder avoids
both lookup functions. This reduces the miss overhead measured in the first
prototype. Cache state and counters occupy less than 0.5 KiB on ARM64;
no heap allocation or new GPU resource is introduced.

Ordinary descriptor updates/binds still occur for every draw. Shader source,
descriptor layouts, dynamic offsets, draw order/count, vertex/index handling
and VP-constant upload/reuse are unchanged. Non-Android and explicit rollback
use the combined original block construction/upload path.

## Selection and counters

Android defaults to enabled. `debug.nfsu.uniform_cache=0` restores construction
and upload on every draw. Exact `1`, empty and unset enable reuse; other
nonempty values disable it. `RECOMP_VK_UNIFORM_CACHE`, when present, takes
precedence, including an empty value. Selection is cached at startup;
restart the process after changing it. No override is needed for the candidate.

Graphics startup logs `[VKUBO] reuse=1`. Every 300 flips the performance log
reports `vs_requests`, `vs_hits`, `fs_requests`, `fs_hits`, `vs_skipped`,
`fs_skipped`, `uploaded_bytes` and `reused_bytes` since the preceding report.
Skipped counts are cooldown bypasses, not hits. Byte counts cover successful
block payloads only, excluding ring alignment padding. Counters use integer
increments; no per-draw timers or property queries are added.

## Validation and limits

- Host ASan/UBSan and Poco ARM64: 14,182 byte comparisons against a literal
  pre-change builder. Cases cover random states, every register, matrix/scale
  components, surface dimensions, NaNs, signed zero, subnormals, rounding,
  native FPCR FZ/DN controls, backoff/recovery, two buffers, generation/rewind,
  command-buffer changes and allocation failure. Selector tests cover ten
  default/rollback/invalid/environment cases and verify uploaded bytes in
  both selected paths.
- Mali-G615 MC2: 4096 compute dispatches read both ordinary UBOs as raw uints.
  All 507,904 components match CPU legacy bytes. Two frame slots, eight epochs
  per mode, two submissions per epoch, repeating inputs and continuous changes
  exercise reuse and cooldown while the first submission can remain pending.
  Reuse records 876 VS hits, 808 FS hits and 896 cooldown skips in each stage.
- NDK fixtures compile with `-O2 -Wall -Wextra -Werror`. Final APK
  `assembleDebug lintDebug` passes with register-block/TLS-argument codegen;
  existing renderer warnings are unchanged.

The final isolated CPU benchmark uses 256 epochs of 2048 draws per mode,
three alternating pairs, the real helper and a bounded mapped-ring adapter.
It includes key lookup/backoff, block building, allocations, counters and
a checksum; it excludes Vulkan calls and game work. Only the standalone
process is pinned to CPU7 (`taskset 80`); clocks and game settings are unchanged.

| Input pattern | Legacy ns/draw | Reuse ns/draw | Allocations across 524,288 draws |
| --- | --- | --- | --- |
| Repeated within each epoch | 69.21–72.67 | 32.35–32.50 | 1,048,576 → 512 |
| Changes every draw | 72.77–77.16 | 77.23–84.84 | 1,048,576 → 1,048,576 |
| Changes every four draws | 71.66–73.02 | 46.33–46.68 | 1,048,576 → 262,144 |

Paired ratios are 2.13–2.24x faster for repeated inputs and 1.54–1.58x faster
for groups of four. Continuous changes still cost 2.4–11.2% more for this
isolated work. The earlier unpinned run has large clock/migration variation;
it is not used to establish a speedup. These synthetic patterns do not
establish the game's hit rate, FPS, frame pacing, power or heating benefit.
Subsequent gameplay feedback and real race counters are recorded below.
No game install/restart or property change was performed by the agent.

Final device outputs are `gpu.txt`, `device-tests.txt` and
`benchmark-combined-cpu7.txt` under the ignored results directory above its
`baseline/` subdirectory. `artifact.json` records APK SHA and matching packaged /
unstripped build IDs.

## Gameplay verification

The user installed the candidate and reports that gameplay looks correct.
The installed APK SHA-256 matches the final artifact exactly:
`90bdbdf4c694c72bd92551c8dc492dd685121b033ea9fddddaa5301c644d86c3`.
A screenshot confirms an active race on lap 3 of 4. Menu transitions were
not separately reported. The game runs in `org.nfsu.android:game`; its PID
at capture was 8409 and the renderer TID was 15032.

The last ten complete, consecutive 300-flip reports (frames 10500–13200,
13:46:42.154–13:47:27.612 device log time) cover 3000 flips and 7,230,702
uniform requests per stage. Weighted counters are:

- VS: 7,221,702 hits, **99.8755%**, zero cooldown skips.
- FS: 6,795,946 hits, **93.9874%**, 37,240 cooldown skips.
- Uploaded payload: 167,954,304 bytes; reused payload: 3,418,473,888 bytes.
  Together they equal the original 496 bytes times the number of draws.
  **95.3169%** of uniform payload upload is avoided, about 1.087 MiB per flip,
  excluding alignment padding, vertices, indices and other constants.

These counters confirm that the repeated-input case is common in the tested
race. They measure avoided construction/upload work, not an FPS or power
comparison. No off/on gameplay comparison was requested. Logs, screenshot
and the weighted calculation are saved in the ignored results directory's
`candidate/` subdirectory (`counter-summary.json`).

## Reproduction

```sh
python3 tests/vulkan_uniforms.py --sanitize
python3 tests/vulkan_uniforms.py --compile-only \
  --cc "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang" \
  --output-dir /tmp/nfsu-uniform-arm64
python3 tests/vulkan_uniforms.py --gpu --compile-only \
  --cc "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang" \
  --output-dir /tmp/nfsu-uniform-gpu
"$ANDROID_NDK/shader-tools/darwin-x86_64/glslc" \
  tests/vulkan_uniforms.comp -o /tmp/nfsu-uniforms.spv
adb push /tmp/nfsu-uniform-arm64/vulkan-uniforms \
  /tmp/nfsu-uniform-gpu/vulkan-uniforms-gpu /tmp/nfsu-uniforms.spv /data/local/tmp/
adb shell /data/local/tmp/vulkan-uniforms
adb shell /data/local/tmp/vulkan-uniforms-gpu /data/local/tmp/nfsu-uniforms.spv
adb shell taskset 80 /data/local/tmp/vulkan-uniforms --bench
```
