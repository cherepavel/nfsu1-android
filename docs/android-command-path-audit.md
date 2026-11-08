# Android command/resource management audit

2026-10-21, ASUS Zenfone 7 Pro, Camera menu. The investigation moved from
shader/attachment hypotheses to the compatibility layer and Vulkan command
management. No heating/high-GPU problem is declared solved.

## Confirmed path and control sample

The guest game produces Xbox/NV2A pushbuffer commands. Native C code in
`nv2a_pb_scan.c` / `nv2a_pb_exec.c` interprets their packet/state semantics and
passes raw batches to `nv2a_vk.c`, which records Vulkan commands. The vertex
and fragment shaders execute as compiled SPIR-V. The Android Vulkan backend
advertises RAW_DIRECT and supplies supported attributes in their stored formats;
this is not a full CPU rasterizer or a per-fragment Xbox interpreter.

The legacy `xbox_FramebufferWindowPresent` call before backend flip looked
like a possible duplicate path. Inspection of `fb_present.c` rules it out for
Android: its real implementation is Windows-only, with no-op functions on
other platforms. This is source evidence, not a general claim that every
command in a frame is necessary.

The first 15-second simpleperf cpu-cycles/callchain capture used PID 10137,
renderer 10274, GPU profiling=2, kick=0, all unsuccessful experiments off.
It contained 4,737 samples (no loss). Nested CPU time around descriptor
management was substantial, but internal performance timing was also active.

A clean second capture used the **same installed APK**, PID 17520, renderer
17651, started at 12:42:08 MSK. Startup logs confirm `performance counters=0`,
GPU timestamps=0, kick=300, alpha/zero-source=1, compact/deferred clears=0,
unused_depth/pass_barrier=0. Camera was restored, with 250%, 60 FPS, FXAA,
Square Pixels, reflections and 16x anisotropy; Blur/Bloom off. The 15-second
capture at 12:44:55–12:45:11 has 4,991 samples (no loss), 1,936 on the renderer.
It used the SDK simpleperf app profiler without launching or changing the APK,
without adb root, and was symbolized before the subsequent local native-path
build overwrote the unstripped library. Reports and raw data are saved locally.

Renderer-only cpu-cycles report, inclusive (`Children`) unless specified:

| Function | Renderer sampled CPU cycles |
| --- | ---: |
| `vk_draw_raw_impl`, including its callees | 62.61% |
| `android_push_descriptors`, including driver calls | 16.54% |
| `vkAllocateDescriptorSets`, driver subtree | 8.58% |
| `vkUpdateDescriptorSets`, driver subtree | 5.82% |
| `nv2a_pb_exec_run`, self | 8.18% |
| `tex_get`, self | 6.74% |

These entries overlap and must not be added. They are CPU cycle shares among
running samples, not GPU busy time, wall waits, power or expected speedups.
The two snapshots change both diagnostics and kick policy and are **not** an
A/B measurement of diagnostic savings. The clean profile establishes that
descriptor management remains a hotspot after removing internal profiling.
Animation and CPU/GPU clocks were not locked; the APK is a debug build.

## An available native API was bypassed

`cmd gpu vkjson` on this phone lists **VK_KHR_push_descriptor**. However,
Android device setup unconditionally excluded that extension and selected
`android_push_descriptors`. Its implementation allocates a new descriptor set,
updates it and binds it on every guest draw and final FXAA. With approximately
630 actual draws/frame, this implies tens of thousands of set allocations and
updates per second. This operation count follows source plus earlier recorded
draw counts; no new API-call counter was added to the clean run.

The desktop backend already uses `vkCmdPushDescriptorSetKHR`. Push descriptors
are independent of dynamic rendering and dynamic vertex input, which this
older Android driver need not support. Treating all those features as one
Android fallback policy bypassed an available resource-binding command.
Both paths are Vulkan. This finding concerns binding/driver management, not
GPU shader interpretation.

## Prepared opt-in change

Android startup property `debug.nfsu.native_push=1` requests the native path.
Default/unset/0 uses the original descriptor-set implementation. It:

- Enumerates device extensions and queries `maxPushDescriptors`, requiring at
  least 7 (three UBOs and four combined image samplers). Enumeration failure,
  incomplete results, missing extension or insufficient limit uses fallback.
- Enables only the push-descriptor extension, preserving Android's existing
  render-pass and vertex-state adapters. If optional-extension device creation
  fails, it retries without the extension. If the function is missing, it
  retains fallback before creating any descriptor layouts.
- Marks **both** guest and FXAA set layouts as push layouts when native is
  selected and dispatches the same descriptor writes to the native entry point.
  Fallback layouts/commands remain unchanged. It avoids app-side per-draw
  descriptor allocation/update/bind; actual driver behavior still needs profiling.
- Changes no shader source, graphics settings, draw order/count, uniforms,
  textures, barriers, render target, pass or frame-ring/fence lifetime.

`[VKDESC] requested=N supported=M max_push=L native=K` confirms the selected
startup path. **Restart is required**: layout flags cannot switch live under
existing pipelines. See the [Khronos push-descriptor sample](https://docs.vulkan.org/samples/latest/samples/extensions/push_descriptors/README.html)
and [descriptor specification](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html)
for the API/limit/layout requirements.

## Validation and next comparison

`tests/vulkan_push_descriptors.c` passes host ASan/UBSan for explicit opt-in,
extension/error/limit checks and layout flags. Its headless device probe uses
the production capability/layout helpers: Adreno 650 reports **32** push
descriptors; device creation with the extension, native entry-point lookup,
seven-binding push layout and pipeline layout all succeed. It does **not**
draw or establish image equivalence/performance. The debug APK passed
`./gradlew assembleDebug lintDebug` before the user's manual signing and
installation. The agent does not install it or change package/APK path.

The next installed run starts with native_push=0, perf=0, gpu_profile=0,
kick=300, unused_depth=0, pass_barrier=0. Keep all saved quality options.
Record short warmed renderer CPU instruction/cycle profiles and presentation
samples; then restart the same APK with native_push=1, and reverse/repeat.
Check startup selection each time. Native captures should show no
`android_push_descriptors`/descriptor allocation subtree on that command path.
The native command itself and all rendering still have costs: require measured
instruction/CPU improvement and unchanged output. Separately assess GPU/thermal
behavior; a CPU win alone does not resolve 90% GPU or establish energy savings.
Do not resume shader/attachment experiments alongside this test.

Raw `native-command-*` captures, reports, capability data and device state are
ignored files under `performance-results/2026-10-21/asus-menu/`.

## Installed APK: preliminary baseline, 12:55–13:04 MSK

The user installed the new APK and opened Camera. The renderer belongs to
`org.nfsu.android:game` **PID 23607 / TID 23675**, not the launcher process
23535. Startup confirms `[VKDESC] requested=0 supported=1 max_push=32 native=0`,
perf/timestamps=0, kick=300, and the unsuccessful attachment/barrier trials off.
The screenshot confirms the required quality settings, but ASUS touch-lock
subsequently covered and dimmed the scene. The user was asked to remove it
before the main comparison. No native-mode run has been measured yet.

Preliminary captures behind that overlay are deliberately named
`native-push-a1-touchlock-*` and must not be treated as the clean A/B baseline.
The 13:01:34–13:01:49 renderer-only stat counts 13.858 billion instructions,
7.324 billion cycles and 6.925 seconds of task-clock in 15.001 seconds.
The separate 13:03:41–13:03:57 app callchain capture has 7,217 samples without
loss, including 2,774 renderer samples. In that renderer report the fallback
descriptor subtree is 15.35% inclusive (allocation 6.70%, update 6.19%; nested,
not additive). This confirms the hotspot in the installed build, without
establishing savings, GPU load, power, or a per-frame instruction count.
SurfaceFlinger latency returned no valid frame rows for this capture.

## Completed native push 0 → 1 → 0 → 1 comparison

All four processes used the same installed APK, perf/timestamps=0, kick=300,
and the full saved quality settings. Touch-lock captures above are excluded.
Camera screenshots confirm the scene/settings. The user reported no visible
change in native mode. Sampling and stat were separate; an initial overlapping
A1 record attempt produced a `Text file busy` copy error and is excluded.
Use `native-push-a1-cpu-retry.data` for the accepted A1 callchain recording.

Renderer-only simpleperf stat, each approximately 15 seconds:

| Run / native | PID / renderer TID | Stat start MSK | Instructions, billion | Cycles, billion | Task-clock, seconds | Effective CPU GHz |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| A1 / 0 | 23607 / 23675 | 13:04:54 | 14.011 | 7.433 | 7.039 | 1.053 |
| B1 / 1 | 28419 / 28481 | 13:08:53 | 13.672 | 9.023 | 8.505 | 1.059 |
| A2 / 0 | 31003 / 31066 | 13:11:49 | 13.847 | 7.414 | 6.751 | 1.095 |
| B2 / 1 | 32326 / 32387 | 13:13:50 | 13.670 | 10.271 | 7.223 | 1.420 |

The native runs reduce instructions/second by 2.41% and 1.28% against their
preceding control. CPU cycles/second rise 21.41% and 38.54%; task-clock rises
20.83% and 6.99%. B2's CPU clock differs substantially, so these are observed
costs under the governor, not a fixed-frequency hardware efficiency estimate.
The A1/B1 effective clocks are close; neither comparison establishes a win.
Frame counts are unavailable from the latency dump; screenshots show roughly
59–60 FPS but do not prove sustained presentation cadence. No GPU timestamps,
energy measurement or cooling improvement was obtained in this CPU test.

Separate 15-second renderer callchains explain the rejection. The old fallback
is 13.66% / 15.50% inclusive sampled CPU cycles in A1/A2; native
`vkCmdPushDescriptorSetKHR` is **32.25% / 31.31%** in B1/B2. The old allocation
and update subtree disappears, but copying within the driver grows (`memcpy`
self 14.12% / 14.39% in B1/B2). Percentages are nested CPU samples, not additive
or GPU timing. Native callchains have 3,368 / 3,349 renderer samples (raw app
captures 8,008 / 7,788, none lost); see the actual reports for exact totals.

The first A2 startup at 13:09:57 crashed in `xbox_mmio_read` from a guest thread
before the renderer initialized. This occurred with native_push=0; the crash
log is preserved and the successful retry above is used. Its cause is not
established by this experiment. At 13:15:04 the property was restored to 0 and
the app restarted; the restored process is PID 1916 / renderer 2005. Do not
enable the native experiment by default. The matching old unstripped library
was saved as `native-push-libnfsu2_recomp.so` before the next build.

Raw measurements and normalized deltas are in `native-push-abab-summary.json`.
This rejects a particular driver API on this device; it does not vindicate
per-draw set allocation or resolve the original GPU/heat issue. The next
specific change reduces allocation/update calls via
[per-frame immutable descriptor reuse](vulkan-dynamic-descriptors.md), rather
than routing every original write to a different driver entry point.
