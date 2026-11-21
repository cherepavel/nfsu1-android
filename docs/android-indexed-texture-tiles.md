# Android paletted texture tiles

13 November 2026. Branch `codex/tiled-indexed-textures` from `develop`
at `b9e1a83`, after the tested uniform-block reuse merge.

## Current profile

The user continued driving on Poco 24095PCADG / Mali-G615 MC2. A renderer-only
CPU-clock profile at 1000 Hz over 20 seconds collected **17,778 samples,
zero lost**, on game PID 8409 / renderer TID 15032. Screens surrounding the
capture show racing. Installed APK SHA-256 matches the previous tested build
`90bdbdf4c694c72bd92551c8dc492dd685121b033ea9fddddaa5301c644d86c3`;
saved native symbols match build ID `d7da6758ed0a0e043728e0390ef9a877311702b6`.
That renderer source is the current develop version with uniform reuse.

`tex_get` has 10.732% self samples (11.987% inclusive). Source attribution
places **3.065% renderer samples** in the scalar `decode_indexed` loop and
about 3.785% in sampled `bytes_hash`. These are shares of renderer CPU samples,
not frame-time savings. Only the indexed decoder is changed here. The baseline
profile, library, pre-change source and attribution are saved under ignored
`performance-results/2026-11-13/indexed-textures/baseline/`.

## Selected implementation

For P8 / color `0x0B`, the old decoder walks each output row and computes every
pixel through an x-address table, an index-byte load and a palette lookup.
The new Android ARM64 path processes **8x8 Morton tiles**. The low three x/y
bits interleave into six bits, making each tile exactly 64 contiguous source
bytes. The existing x/y tables locate its base. Four 4x4 quadrants use fixed
source offsets; scalar palette gathers produce four uint32 colors at a time
and NEON stores write them into the corresponding output row.

Eligibility is deliberately limited to power-of-two width and height,
**8–512 inclusive on each axis**. Other shapes return from the tile helper
before reading/writing any pointers and then use the existing scalar loop.
Non-Android keeps its previous loop. Builds without ARM64 NEON also fall
back. Prototype scalar tiles, narrower tiles and explicit vector stores were
measured; the selected 8x8/vector-store path wins on representative sizes.
1024x1024 did not reliably benefit, so it is excluded. No unused prototype
variant is shipped; prototype source/output is retained only with local results.

The existing palette copy is retained: 256/128/64/32 entries, with indices
beyond the selected size resolving to entry zero. A missing palette retains
the existing failure return without output writes. Integer gathers/stores
preserve every color/alpha bit; there is no floating-point calculation or
approximation. Source reads and destination writes stay inside each complete
tile. Byte-aligned source data and all valid uint32 output alignments work.

Texture hashing/cache decisions, upload byte counts, GPU images/descriptors,
mip generation, format, barriers, ring allocation and fence lifetimes are
unchanged. There is no new thread, heap allocation or GPU resource. The
1024-byte local palette and full swizzle tables already existed; diagnostic
state adds six uint64 counters and one cached mode integer.

## Selection and counters

Enabled by default on Android; acceleration requires ARM64 NEON and eligible
dimensions. `debug.nfsu.indexed_tiles=0` restores the per-pixel loop.
Unset, empty and exact `1` enable; other nonempty values disable.
`RECOMP_VK_INDEXED_TILES`, if present, takes precedence, including an empty
value. Restart after changing the cached selector. The candidate needs no
property override.

Startup reports `[VKP8] tiles=1 neon=1 max_dimension=512` on ARM64.
Every 300 flips the performance log reports counts since the last report:

- `calls`: indexed decode requests while enabled.
- `tiled` / `scalar`: successful calls through each path.
- `invalid`: calls rejected because the palette is missing.
- `tiled_pixels` / `scalar_pixels`: processed pixels in the corresponding path.

`calls = tiled + scalar + invalid`. Counts exclude unchanged textures that
return from the texture cache before decoding. There are no per-pixel counters,
clocks or property queries. `[VKP8]` is separate from existing `[VKIDX]` index
upload counters. Mode zero does not collect these counters.

## Validation

- The fixture extracts the real production selector, dispatch, palette setup,
  swizzle tables and scalar fallback. Its oracle is the literal decoder from
  develop before this change.
- **2320 images / 41,691,008 pixels** compare byte-for-byte: power-of-two aspect
  ratios through 4096 within bounded image areas, 512x512, 1024x1024, tiny and
  non-power-of-two shapes, random/ramp indices, all four palette sizes, all 64
  missing-palette flag combinations, source/palette byte alignments 0–3 and
  output alignments 0/4/8/12 modulo 16. Source buffers end exactly at their
  required storage size; output guards and ASan detect excess accesses.
- The real decoder must rebuild poisoned swizzle tables independently after
  each oracle call. Shape rejection is also tested with null inputs and an
  unchanged output sentinel, ensuring it cannot partially write then fall back.
- Host ARM64 ASan/UBSan passes for NEON, forced-scalar and non-Android policies.
  Ten separate-process selector cases verify defaults, invalid values,
  environment precedence, cached choice and actual selected dispatch.
- NDK ARM64 (`-O2 -Wall -Wextra -Werror`) passes all image cases on Poco.
- Existing production texture-cache scalar/snapshot integration checks pass
  ASan/UBSan, including palette/cube/format changes, collision fallback,
  allocation failures and eviction ownership.
- `assembleDebug lintDebug` passes with register-block/TLS-argument codegen.
  Existing warnings remain. Packaged/unstripped native build IDs match and
  packaged selector/log markers are verified.

## Isolated CPU measurements

The final benchmark invokes the real old/new decoders, including palette
copy/padding, swizzle-table check, cached selection, shape dispatch and
diagnostic counters. One source or eight rotating sources are used, with
256- and 64-entry palettes. Each path/pair processes 16,777,216 pixels;
three pairs alternate order. Full byte validation precedes benchmarking;
paired checksums agree. A warmup decode loop precedes each size/working set.

Only the standalone test is pinned to CPU7 with `taskset 80`; game scheduling,
device clocks and properties are unchanged. Vulkan upload and rendering are
outside timing. Final 256-entry results, ns/pixel across three pairs:

| Shape / source count | Legacy | Tiles | Paired speedup |
| --- | --- | --- | --- |
| 8x8 / 1 | 1.5638–1.5688 | 1.0732–1.0744 | 1.46x |
| 64x64 / 1 | 1.0430–1.0489 | 0.5344–0.5349 | 1.95–1.96x |
| 128x512 / 1 | 1.0134–1.0156 | 0.5832–0.5858 | 1.73–1.74x |
| 128x512 / 8 | 1.0499–1.0937 | 0.5918–0.6074 | 1.76–1.83x |
| 512x128 / 1 | 1.0656–1.0937 | 0.5973–0.6039 | 1.77–1.82x |
| 512x512 / 1 | 1.1156–1.2331 | 0.6374–0.6841 | 1.74–1.82x |
| 512x512 / 8 | 1.2345–1.2902 | 0.7255–0.9464 | 1.32–1.78x |

The 4x512 fallback is about 0.99–1.00x legacy in this run. The 1024x1024
fallback has greater variation: paired ratios 0.91–1.02x for one source and
0.97–0.99x for eight sources (256-entry palettes). Fallback retains the
per-pixel algorithm with selector/dispatch/counter overhead; no speedup is
claimed for excluded shapes. An earlier final-code run has different absolute
times as clocks vary; the reported table uses `benchmark-final.txt` only.

These measurements establish local decode cost on Poco, not game-wide FPS,
frame pacing, heat or power improvement. Subsequent gameplay feedback and
`[VKP8]` coverage are recorded below. No game installation, restart, input or
property change was made by the agent.

Raw logs, fixtures, benchmark output and `benchmark-summary.json` are under
the ignored results directory. `artifact.json` records exact source hashes,
APK SHA-256 `63f495727c0162303ea14523226bfb5a0dec55375451363e113542875f924dcf`
and native build ID `86255fc30cbd705368e8ea3b701c24dc48877385`.

## Gameplay verification

The user installed the candidate and reports that everything looks correct.
The installed APK SHA-256 matches the artifact above. Startup reports
`[VKP8] tiles=1 neon=1 max_dimension=512`; the game PID at capture is 12290,
renderer TID 14304.

The last ten consecutive, complete 300-flip reports (frames 3900–6600,
15:09:19.044–15:10:08.736 device log time) cover 3000 flips:

- 6891 indexed decode calls: 6887 tiled, four scalar, zero invalid palettes.
  Tile coverage is **99.9420% of calls**.
- 741,403,648 tiled pixels and 655,360 scalar pixels:
  **99.9117% of decoded pixels** use the new path.

Thus eligible shapes cover nearly all indexed decoding in this observed
gameplay interval. Counts do not measure an end-to-end speedup, FPS or power
benefit. Logs, screenshot and weighted `counter-summary.json` are retained
under the ignored results directory's `candidate/` subdirectory.

## Reproduction

```sh
python3 tests/vulkan_indexed_textures.py --sanitize
python3 tests/vulkan_indexed_textures.py --scalar --sanitize --output-dir /tmp/p8-scalar
python3 tests/vulkan_indexed_textures.py --non-android --sanitize --output-dir /tmp/p8-desktop
python3 tests/vulkan_indexed_textures.py --compile-only \
  --cc "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang" \
  --output-dir /tmp/p8-arm64
adb push /tmp/p8-arm64/vulkan-indexed-textures /data/local/tmp/
adb shell /data/local/tmp/vulkan-indexed-textures
adb shell taskset 80 /data/local/tmp/vulkan-indexed-textures --bench
```
