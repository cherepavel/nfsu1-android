# Android fused index upload

12 November 2026. Feature branch `codex/fused-index-upload`, based on
`develop` at `1186a9b`. This continues the vertex-upload work without changing
the existing vertex cache or graphics settings.

## Data path

The original executor expands packed Xbox indices to its uint32 array, scans
the minimum/maximum for vertex gathering, then writes a second uint32 array
subtracting the minimum. Vulkan optionally rewrites that array for LINE_LOOP
or QUADS, then copies/narrows the result into the mapped upload ring.

Android Vulkan now advertises `NV2A_BACKEND_INDEX_BASE`. The executor supplies
its original uint32 array plus `Nv2aRawBatch.index_base`, skipping allocation
and filling of its rebased index scratch array. Vulkan plans topology/count/
index width without reading or allocating indices, then writes the final
payload directly into the existing ring. Ordinary primitives use NEON
subtract-and-narrow on ARM64, with a scalar tail. The 32-bit path uses vector
subtraction, or memcpy when the base is zero. QUADS expand directly to their
six triangle indices and LINE_LOOP appends its closing index directly.

This removes one intermediate uint32 write/read cycle for ordinary draws,
and also the primitive-expansion scratch array for LINE_LOOP/QUADS. The
initial Xbox packet unpacking, min/max scan, vertex gathering, final upload
size, topology, index order, draw order/count and vertexOffset=0 are retained.
The existing full-draw ring reservation still happens before any draw upload;
it conservatively reserves four bytes per output index, as before.

Width selection uses the gathered vertex span: at most 65536 vertices uses
uint16, otherwise uint32. The original index may exceed 65535; subtraction
happens before narrowing. Unsigned wrapping and incomplete trailing QUADS
follow the previous implementation. Overflowing rewritten counts are rejected
before any source read or allocation.

## Compatibility and lifetime

Only Android Vulkan requests the new capability. Other backends retain the
rebased array and base 0. A registered `nv2a_raw_batch_hook` also forces that
legacy representation, preserving its contract. Borrowed source indices are
valid only until the synchronous `draw_raw` call returns. Vulkan consumes them
within that call; pending instancing still owns an index copy. Its equality
key now includes the index base to prevent merging different gathered spans.

There is no GPU access to guest/temporary CPU memory: final indices still live
in the existing frame-fenced upload ring. Command recording, index-buffer
alignment, synchronization and queue submission use the previous mechanisms.
Desktop/Switch Vulkan and OpenGL keep their previous paths.

## Selection and counters

Fused uploads are enabled by default on Android. `debug.nfsu.index_fuse=0`
restores both executor rebasing and the previous Vulkan primitive/packing
path. Exact `1`, an empty property or an unset property selects fused uploads;
invalid nonempty values select the old path. `RECOMP_VK_INDEX_FUSE`, when
present, takes precedence, including an empty value. Selection is cached at
renderer installation; fully restart the game after changes. Startup logs
`[VKIDX] fused=1` or `0`.

Counters are separately opt-in: `debug.nfsu.index_stats=1` or
`RECOMP_VK_INDEX_STATS=1`, with environment precedence. They report every 300
flips and reset after each report:

- `uploads16` / `uploads32`: successful index upload operations by width.
- `source_indices` / `written_indices`: counts before/after primitive expansion.
- `upload_bytes`: final payload bytes written to the ring.
- `nonzero_base_indices`: source indices rebased inside the fused uploader;
  this excludes borrowed batches whose minimum is zero.
- `expansion_bytes_avoided`: uint32 scratch writes avoided for LINE_LOOP/QUADS.

These are operations/bytes, not CPU/GPU durations or physical memory traffic.
The base-zero cases mean `nonzero_base_indices * 4` is only a lower bound on
executor scratch bytes eliminated. Uploads retained for pending instancing
are not repeated in these counts. Counters are off by default and make no
per-draw clock calls.

## Validation

`python3 tests/vulkan_index_upload.py --sanitize` compares the actual fused
planner/packer and executor bridge helpers against the production legacy
primitive rewriting plus an independent scalar reference for final packing.
45,280 differential cases cover every primitive, empty/short/incomplete
inputs, 16/32-bit boundaries, nonzero/high bases, unsigned wrapping, unaligned
source positions, source immutability, output canaries, simulated ring reuse
and allocation failure. Tests also exercise hook/backend fallback and the
actual pending-instancing equality function with changed index bases.
Separate processes verify the real startup selectors, default-on/rollback,
statistics default-off, invalid inputs and environment precedence.

Host ARM64/NEON and x86_64/scalar ASan/UBSan runs, plus the standalone NDK
ARM64 regression on Poco 24095PCADG, pass. The existing 4,547,796-case ARRAY_ELEMENT16 unpack regression
and vertex-upload-cache ASan/UBSan regression also pass. The feature APK
passes `assembleDebug lintDebug` with both normal executor flags retained.

The standalone test records Vulkan binding arguments with a mock; it does
not submit GPU rendering or establish whole-game visual equivalence. After
the initial delivery the user installed/launched the APK; the first race
check is recorded below. No matched in-game off/on comparison is required
by this work.

## Isolated Poco CPU experiment

An NDK O2 standalone utility runs the production old/new CPU algorithms on
100,000 batches of 2048 source indices per pass, three alternating pairs for
each primitive. It uses CLOCK_THREAD_CPUTIME_ID and verifies equal checksums:

| Primitive | Previous CPU ms | Fused CPU ms |
| --- | --- | --- |
| TRIANGLES, pairs 1/2/3 | 39.467 / 39.197 / 39.256 | 26.314 / 26.075 / 26.240 |
| QUADS, pairs 1/2/3 | 184.038 / 183.865 / 183.002 | 82.768 / 83.395 / 82.377 |

This is about 1.5x / 2.2x faster for these isolated algorithms. The fixture
uses warm ordinary CPU memory, not actual Vulkan-mapped memory; it excludes
driver calls, command recording and GPU execution. Phone clocks were not
locked and the game was not restarted or reconfigured. No FPS, whole-renderer,
power or thermal improvement is inferred from these results.

Reproduce by compiling `tests/vulkan_index_upload.py` with the NDK compiler
and `--compile-only`, then running the produced binary with `--benchmark`.
Raw outputs are ignored local artifacts under
`performance-results/2026-11-12/index-upload/`.

Candidate APK: `android/app/build/outputs/apk/debug/app-debug.apk`.
SHA256: `11508fff460ff838455dac72f5ee6b37cc462ab61ea991757f8e59a708ce4c5d`.

## First installed race check

The user installed/launched the feature APK and signalled an active race on
Poco 24095PCADG. The 12 November 2026 startup at 22:20:09 MSK confirms
`[VKIDX] fused=1` without an index_fuse property override. Vertex cache mode
remains 1. Game PID is 8002, renderer TID 22479. Index statistics are unset
and therefore off; no per-frame index counts or avoided-byte totals were
collected from this run. No game settings/properties were changed and no
installation or restart was performed by the profiling step.

A 20-second renderer cpu-clock/1000 Hz DWARF recording at approximately
22:21:54–22:22:14 MSK contains 15,607 samples with zero losses. Recorded ELF
build ID `939c4b239343f670ca5ca0e82d173f676c9c230e` matches the local library.
`upload_fused_indices` appears in the profile (1.243% self / 2.102% inclusive
renderer sampled CPU), confirming execution beyond the startup selector.
Those shares overlap and describe the enabled path, not measured savings.

Captured graphics/system/runtime logs contain no ERROR/FATAL entries for the
current launch, and the game PID remains 8002 after recording. The screenshot
was collected afterward and shows the race without an obvious geometry defect
in that frame; it does not establish temporal or pixel-equivalent correctness.
There is no isolated in-game CPU/FPS/power improvement claim from this capture.

After this race check the user reported that the game felt slightly smoother
("будто бы чуть плавнее"). This is subjective gameplay feedback, recorded
separately from the CPU profile and isolated algorithm benchmark; it does not
establish a numeric FPS or frame-time improvement.

Raw profile, matching symbols, state, options, logs, error check, post-record
screen and CPU attribution are saved in ignored local artifacts under
`performance-results/2026-11-12/index-upload/race-on/`.
