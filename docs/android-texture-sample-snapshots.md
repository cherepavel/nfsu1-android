# Android small-texture sample snapshots

13 November 2026. Branch `codex/renderer-hotpath`, based on `develop`
`72ce419`, after the user-tested bulk-constant change was merged (`1d10a2e`).

## Evidence and selected scope

A fresh Poco race profile on 12 November, after bulk constants and the runtime
query change, collected 14748 renderer CPU-clock samples at 1000 Hz over
20 seconds with zero loss (PID 9842, TID 18627). The installed APK matched
SHA256 `edcaaffd59661fb630b6768df955f08006faa27b30ea280ee65cc6a30bdac0a0`.
Screens before/after show racing. No installation, restart or input was made
by profiling. Matching native symbols attribute 6.855% renderer self samples
to `tex_get`, including about 4.895% to its inlined sampled FNV calculation.
These are sampled shares, not expected savings. Raw data and source-line
attribution are under `performance-results/2026-11-12/renderer-hotpath/`.

Simply unrolling FNV did not reliably improve the large-texture cases on
Poco. Comparing previous sample words removes the serial multiplication chain
on unchanged input, but an unrestricted snapshot path also lost performance
on some larger working sets on both devices. Prefetching did not reliably
resolve that. Those variants are not shipped.

The final optimization is deliberately limited to source spans of **1024–4096
bytes inclusive**, where device measurements support it. Smaller and larger
spans retain the existing sampled hash. This addresses only part of texture
validation, not the full 4.895% sampled cost, decoding or GPU texture upload.

## Behavior

For eligible textures, the CPU saves the exact words the legacy hash read.
On a later eligible check it compares all those words, including the repeated
final word, using four independent difference accumulators. If they match,
their legacy hash must match as well, so the existing image view can be reused.

The stored byte count, palette mode and hash must also agree with the current
entry. Paletted textures compare the same 1024-byte palette sample sequence.
Palette address is not an identity requirement: equal palette contents retain
the legacy result. The existing once-per-frame `checked` guard remains first.
Dynamic render-target views still follow their existing separate path.

On any mismatch, the code captures samples **while computing the exact old
FNV result**, then uses the previous hash/update decision. A mismatch of words
does not force an upload: even a constructed FNV collision still follows the
old hash-hit outcome. Changed eligible data thus does additional comparison
work before the old hash decision; its game-wide incidence is not measured
for this candidate. No source-checking addresses or sampling frequency are
removed, and no new content hash is introduced.

Captures use renderer-owned scratch until the cache-entry decision is complete,
so layout replacement and eviction cannot leave a pointer into freed snapshot
memory. Snapshot allocation failure retains the scalar hash result and image.
Snapshots are freed with `tex_free`, independently of deferred GPU retirement.
Only the renderer thread accesses these CPU snapshots. Vulkan image, view,
descriptor, upload-ring and fence lifetimes are unchanged.

Each snapshot requests 3088 bytes. At the existing 1024-entry texture limit,
payload allocation is at most 3.02 MiB, plus allocator overhead, an 8 KiB
pointer-array-size increase on ARM64 and one 3088-byte scratch snapshot. Only
eligible entries allocate a snapshot; the regular texture budget still counts
GPU texture bytes rather than this CPU metadata. Unchanged checks allocate
nothing after warmup.

## Selection and diagnostics

Default on for Android. `debug.nfsu.tex_samples=0` restores scalar hash checks.
Unset, empty and exact `1` enable; invalid nonempty values disable.
`RECOMP_VK_TEXTURE_SAMPLES`, if present, takes precedence, including empty.
Fully restart after changing the cached selection. Other platforms keep their
previous path. Startup reports `[VKTEXS] enabled=1 bytes_per_entry=3088`.

Every 300 flips, `[VKTEXS]` reports counts since the last report:

- `checks`: existing snapshots considered, including metadata misses.
- `hits`: complete matching snapshots that avoided recalculating FNV.
- `captures`: eligible fallback hash/sample captures, including first use.
- `allocations` / `oom`: snapshot allocation success/failure counts.
- `skipped`: checks outside the selected 1–4 KiB span range.
- `live` / `live_bytes`: current snapshot count and requested payload bytes.

Same-frame hits return before these counters, as before. There are no new
per-check clocks or repeated property reads; existing texture-stage profiling
continues to cover both decision paths.

## Validation and device measurements

- Host ARM64 ASan/UBSan and NDK30 ARM64 on the connected Xiaomi tablet
  24076RP19G, Android API 36: **594960** comparisons against the extracted
  production FNV function pass. They cover byte counts, source alignments,
  exact-end allocations, tails, every sampled-position mutation on larger
  representative spans, bit-exact capture and snapshot change detection.
- A second fixture uses actual production `tex_get`, free, index, account,
  selector and snapshot-store code with GPU allocation/upload stubs. Scalar
  and snapshot modes make matching hash, image-reuse, upload and retirement
  decisions through frame changes, exact eligibility boundaries, palette and
  cube inputs, layout replacement, stale metadata, a constructed FNV collision,
  snapshot/image allocation failures and eviction past cache capacity. All CPU
  snapshots are released. It passes ASan/UBSan and on the tablet.
- Separate-process selector tests cover defaults, cached selection, invalid
  values, explicit rollback and environment precedence.
- `assembleDebug lintDebug` succeeds with grouped registers and the executor
  argument enabled; existing compiler/lint warnings remain. Packaged native
  symbols and startup selector are verified; no app installation was performed.

The final standalone CPU benchmark uses the actual snapshot structure, one
repeated source or a rotation of 32 sources, three alternating path pairs,
and 200000 checks per path/case. Captures/setup are outside timing. It exercises
the production capture/compare helpers and scalar reference, excluding Vulkan,
cache lookup, allocation and rendering. Frequencies and affinity are not locked.

Tablet results, ns/check (range across three pairs):

| Span / working set | Scalar | Snapshot | Pairwise speedup |
| --- | ---: | ---: | ---: |
| 1 KiB / one source | 530.053–530.895 | 278.232–278.573 | 1.90–1.91x |
| 1 KiB / 32 sources | 542.144–611.079 | 305.304–305.941 | 1.77–2.00x |
| 2 KiB / one source | 529.964–530.713 | 278.134–278.415 | 1.91x |
| 2 KiB / 32 sources | 545.042–545.373 | 324.068–325.891 | 1.67–1.68x |
| 4 KiB / one source | 530.328–531.475 | 278.318–304.528 | 1.74–1.91x |
| 4 KiB / 32 sources | 550.042–551.815 | 414.736–415.107 | 1.33x |

Raw final results are under `performance-results/2026-11-13/texture-samples/`.
These are unchanged-input check results on the tablet, not whole-game or Poco
FPS/CPU/temperature/power gains. The user reports that the final APK looks
correct in gameplay and requests merging it into develop. Live counters are
still needed to establish coverage in the actual race; no post-change game
CPU/FPS/temperature measurement was recorded.

APK SHA256: `12ecba545d125922017d5ed74858d53704bddbca7a09e2ef23630226b2cc659d`.
Native build ID: `faee28375371e04fed6f62fa9c799614d9913572`.

## Reproduction

```sh
python3 tests/vulkan_texture_hash.py --sanitize
python3 tests/vulkan_texture_hash.py --integration --sanitize \
  --output-dir /tmp/nfsu-texture-integration
python3 tests/vulkan_texture_hash.py --cc "$ANDROID_CLANG" --compile-only \
  --output-dir /tmp/nfsu-texture-arm64
python3 tests/vulkan_texture_hash.py --integration --cc "$ANDROID_CLANG" \
  --compile-only --output-dir /tmp/nfsu-texture-integration-arm64
adb push /tmp/nfsu-texture-arm64/vulkan-texture-hash \
  /tmp/nfsu-texture-integration-arm64/vulkan-texture-samples /data/local/tmp/
adb shell /data/local/tmp/vulkan-texture-hash
adb shell /data/local/tmp/vulkan-texture-samples
adb shell /data/local/tmp/vulkan-texture-hash --bench
```

`ANDROID_CLANG` is the NDK `aarch64-linux-android30-clang` path. The standalone
commands change no game properties or game state.
