# Android vertex setup: CPU overhead

26 October 2026. The renderer profile in `android-renderer-cpu.md` attributed
3.44% of renderer CPU samples to Mali under the two `vkCmdBindVertexBuffers`
calls in `upload_vertices`.

Android now sends all 16 bindings in one call. Splitting them into two groups
of eight was a Mesa workaround to avoid heap allocation in its entry point;
it is retained on the non-Android path. All buffer handles, offsets, binding
indices and ordering relative to draw remain the same. This does not reuse
vertex data or omit bindings: ring offsets change every draw.

Android also writes the 16 strides/formats directly to the arrays consumed by
`PipeKey`. It no longer initializes two arrays of EXT dynamic-input structs,
compares them with saved structs, or copies them into the saved snapshot.
Android uses baked pipeline vertex input and never calls the dynamic vertex
input extension. The pipeline key receives the same stride and format for
every direct, converted or constant attribute. Desktop dynamic-state caching,
command-buffer generation checks and the two-call Mesa path are retained.

## Validation

`./gradlew assembleDebug lintDebug`: BUILD SUCCESSFUL.
`git diff --check`: passed.

A headless probe on the connected Poco's actual Mali driver records 300,000
sets of 16 valid vertex buffer bindings per pass, with changing offsets.
Each command buffer contains 3,000 sets; command pool reset/begin/end are
outside the timed region. Times use CLOCK_THREAD_CPUTIME_ID. The same real
buffer is bound in both paths; five alternating split/single pairs:

| Pair | Two calls, ms CPU | One call, ms CPU |
|---|---:|---:|
| 1 | 95.006 | 56.267 |
| 2 | 96.395 | 57.966 |
| 3 | 94.562 | 56.586 |
| 4 | 95.232 | 63.385 |
| 5 | 104.486 | 57.317 |

This reduces the isolated recording cost by roughly 40%. The probe does not
submit draws, verify rendered pixels or include the surrounding game CPU work.
It does not establish an FPS improvement or a 40% renderer speedup. It runs
alongside the game with unlocked clocks; an in-game repeat profile is still
needed. The original 3.44% share is an upper bound on the sampled portion
addressed by batching these calls, not the expected total saving. Additional
app-side struct setup savings have not been independently quantified.

The APK is built but not installed; the current race was not interrupted.
Candidate: `android/app/build/outputs/apk/debug/app-debug.apk`, SHA256
`d5e9a3e0de3a8c5928049736dbc6f0d4c10316a77b960c2d4efa8a2837046444`.
Baseline APK saved at `/tmp/nfsu-renderer-vertex-baseline.apk`, SHA256
`56e7dbbfb1379d82b1773b399db8ebae00fb533de3f2673a2d83188edd06585e`.
The candidate also contains the existing index-unpack changes in the workspace.
No descriptor experiment was enabled: the earlier dynamic-descriptor test on
Adreno did not show a repeatable CPU win, and this patch addresses only vertex
setup. Raw benchmark source/results and build log are saved locally under
`performance-results/2026-10-26/vertex-bind/`.

## Installed race profile, 10:51:42–10:52:13 MSK

The user installed the candidate and opened a race. ADB reconnected at
10.20.28.151:37169. PID 14392, renderer TID 3800. A 30-second cpu-clock/1000 Hz
DWARF capture recorded 27,279 samples, lost=0. Installed build ID
`e150b0e66d6108a71ef6a179f2a7007dcf006920` matches the candidate unstripped ELF.
The screenshot shows the race rendering; this is a smoke check, not exhaustive
pixel equivalence or lifecycle validation.

Mali under vertex binding is now a **single callsite**, 2.78% of renderer CPU
samples, versus 3.44% across the old two callsites. This is a reduction of
0.66 percentage points in sampled share (~19% relative), not a controlled
per-operation measurement. The source and callsite confirm that the intended
path is installed. Do not apply the isolated ~40% figure to frame performance.

| Sampled CPU category | Previous race | Candidate race |
|---|---:|---:|
| Mali subtree | 37.55% | 35.60% |
| Vulkan app/framework | 27.70% | 26.26% |
| Xbox commands/geometry | 23.00% | 24.92% |
| Textures app | 6.10% | 7.10% |
| Logging | 5.04% | 5.41% |

The remaining largest driver callsites are still `vkCmdDrawIndexed` (12.13%)
and descriptor allocation/update/bind (5.43% + 2.82% + 0.55%). Those three
non-overlapping descriptor driver entry subtrees total about 8.80%.
`android_push_descriptors` inclusive is 9.47%, including app work.

For full PERF5 windows wholly inside each capture (eight old windows / five
candidate windows), renderer flips/s are 40.68 / 40.82. Draws/frame differ,
2426 / 2291; renderer CPU/input draw is 7.95 / 9.18 us, and CPU utilization
78.49% / 85.84%. Thus there is **no demonstrated overall CPU/FPS improvement**
from these two non-equivalent races. Geometry, textures, waits and governor
conditions differ. A matched reverse/repeat is needed to attribute a net gain.

Eight new hitch intervals in the 52–67 ms range were captured. Several now
have significant texture upload work (17–33 ms), while frames 7562/52 ms,
7864/63 ms, 7881/55 ms and 8210/52 ms still report only 1 ms in textures.
Their running samples are approximately 24, 21, 20 and 36 ms respectively;
waiting/preemption remains unresolved by CPU-only sampling.
Raw data, symbols and analysis are under `vertex-bind/race/` in the local
performance-results directory. The installed candidate and settings are retained.
