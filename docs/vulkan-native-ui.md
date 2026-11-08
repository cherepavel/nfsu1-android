# Android output-resolution UI experiment

Prepared on 28 October 2026. This is an opt-in prototype; device performance
and image quality have not yet been checked. No APK build or installation was
performed by the agent.

The game's menu/HUD draws currently share the scaled scene color target.
Original low-resolution UI textures do not gain source detail from a larger
target, although rasterization, text edges and filtering can still change.
On the measured ASUS setup at 250%, the scene target is 2133x1200; its final
content rectangle is 1920x1080 inside a 2400x1080 display. Drawing UI at the
output size reduces this target area by about 19%, not 6.25 times. This is
only a nominal area reduction, not a measured fragment or memory saving.

## Implementation

Android Vulkan reads `debug.nfsu.ui_native` once at startup. Only exact `1`
enables it; absent, empty and other values disable it. The environment variable
`RECOMP_VK_UI_NATIVE` takes precedence when present. The shipping default
remains off. Other renderers do not change resolution.

The existing `sub_00016460` wrapper surrounds the game's UI production with
private NOP packets in its own D3D pushbuffer. The packet header is 0x00040100;
begin/end payloads are 0x4E554901/0x4E554902. The executor consumes only those
payloads on subchannel 0, method 0x100, outside a primitive. Ordinary NOP traps
retain their previous behavior. This orders UI scope changes with the actual
draws instead of sharing a producer-thread flag with the renderer.

The writer validates the guest device/current-buffer addresses and requires
eight bytes within D3D's existing reserve margin. It skips a marker when that
space is unavailable, without calling a guest allocator or altering guest
registers/stack. Nested wrapper calls emit only the outer pair. Vulkan flushes
held instancing at each accepted marker; flip also resets the UI scope in case
an end marker was skipped. Marker counts must be inspected on the device.

Only a recently presented surface (within four flips) can switch, and only
within the UI scope with depth testing, depth writes and stencil testing all
disabled. No offscreen target is selected and neither dimension may increase.
The first eligible draw:

1. Materializes pending color clears and linearly blits the scene into a
   reusable color image sized to the final logical content rectangle.
2. Exchanges the surface's active image/view/memory and stored dimensions,
   keeping its guest identity and logical dimensions. Its high-resolution
   image remains in the alternate slot.
3. Draws eligible UI without a depth attachment and presents the resulting
   composite through the existing presentation path.

Later non-UI/depth-dependent drawing restores the original image, copying
color back to preserve it. A full RGBA clear proven to cover the entire
logical surface skips this reverse copy. Partial clears cannot discard it.
Render-scale changes restore before resizing; eviction retires the alternate
through the existing frame-fenced garbage mechanism. Output size changes
replace the reusable image. Both paths share the existing presentation fit
calculation, including widescreen, letterboxing and logical rotation extent.

## Costs and visual differences

The scene still rasterizes at the selected 250% setting. This mode adds a
scene-to-output blit and can add a render-pass boundary. Later drawing can
also require a reverse blit. The retained native image adds roughly 8 MiB
per 1920x1080 color surface, excluding allocator overhead. A reduction in UI
area can therefore be outweighed by copying and synchronization.

The earlier linear resolve changes the sampling order: final FXAA reads the
output-sized composite instead of the supersampled scene/UI image. FXAA stays
enabled, but text and 3D edges are not guaranteed pixel-identical. Check these
visually before considering a default change. This experiment does not remove
UI draws, upgrade source textures or establish that UI explains the menu load.

## Validation and device comparison

`tests/vulkan_ui_native.c` includes the production selector and marker decoder
against a mock driver. ASan/UBSan pass for output/AA dimensions, scope/depth
guards, allocation failure, pending color clears, image reuse, full/partial
clear restoration, resize/eviction, stale/offscreen rejection, no upscaling,
and ordinary NOP rejection. A separate process checks the default-off path.
These tests do not execute the guest marker writer or a real Vulkan driver.
The existing presentation geometry test passes 304128 pixel/rectangle checks.
Android syntax checks pass for the three changed producer/executor/Vulkan
translation units using the configured register-block and executor-TLS flags.

The user builds and installs with the established command:

```sh
./gradlew assembleDebug -Pnfsu.registerBlock=true -Pnfsu.executorTlsArg=true
```

Enable before manually restarting the game:

```sh
adb shell setprop debug.nfsu.ui_native 1
```

Keep the Camera menu, graphics settings, state cache, kick setting and GPU
profiling fixed. Confirm `[VKUI] native=1 (startup)` and an expected target
line such as `scene=2133x1200 ui=1920x1080`. Every 300 flips, cumulative
`switches`, `restores`, `preserve_copies`, `ui_draws` and `markers=begin/end`
show whether the path actually ran and whether reverse copies are frequent.
Compare whole game-plus-output queue intervals and pass/blit/draw counts,
not only the shorter UI pass. Inspect screenshots for text/clipping/fades,
scene edges and output aspect. No screenshot/readback frame belongs in timing
windows. Repeat against the same APK with the property set to `0` and a manual
restart. Retain mode off if total cost or visible quality regresses.

## Poco slowdown report, 28 October 2026

The user reported severe slowdown on the connected 24095PCADG, rather than
the previous ASUS. Saved options are scale=100, reflections/FXAA/square on,
anisotropy=16 and fps=60. Renderer TID 12526 in the 13:17:59 startup logs
shows UI marker processing, but all retained VKUI samples through frame 2700
report switches=0, restores=0, preserve_copies=0 and ui_draws=0. At 100% this
experiment's no-upscaling guard bypasses the native target. The proposed
scene/native blits therefore did not execute. Producer markers and their
pending-draw flushes were still enabled; their effect is not yet isolated.

Complete renderer PERF5 windows selected from 13:18:46 onward in the saved
pre-change log give 10.61 flips/s, 43.36% of one CPU core, 54.40% reported
pushbuffer idle waits, and 0.045 ms fence wait per flip. These are host thread
counters, not a GPU busy/time measurement; detailed GPU profiling was off.
No shader compilation appears in the retained hitch samples. The startup
explicitly says guest executor=0, while the previous executor experiments
required runtime executor=1. The two Gradle flags do not enable that runtime
mode. The existing phone environment also enables global TRACE and multiple
input/audio/render traces, unlike a clean default configuration.

For recovery, the agent set ui_native=0 and restored debug.nfsu.executor=1
for the next full process start, without building/installing/restarting,
altering graphics options, changing kick=300, disabling the state cache or
editing the trace environment. Device reads confirm both requested property
values. The user must restart manually before either startup-only setting
takes effect. At this point neither a causal UI regression nor an
executor-related recovery had been established. The subsequent recovery run
below changes two startup modes together and does not isolate their effects.

Raw logs, options, the unchanged trace environment and reproducible interval
summary are under performance-results/2026-10-28/native-ui-regression/.

### Recovery confirmed after the user's restart

The next startup at 13:24:18 confirms guest executor=1; Vulkan confirms
native=0 at 13:24:25. PID 12673, executor TID 18274, renderer TID 18278.
Options and the trace environment are byte-identical to the saved slowdown
configuration. A screenshot taken after the selected timing windows confirms
an active race. No agent build/install/restart, game input, quality change or
trace-file edit occurred.

Five complete PERF5 windows wholly within 13:25:12–13:25:39 contain 1449
renderer flips over 25.0215 seconds: **57.91 flips/s**. Individual windows are
54.73, 59.38, 55.35, 60.14 and 59.95 flips/s. The renderer's reported
pushbuffer idle waits fall to 0.30% of wall time, versus 54.40% in the saved
slowdown summary. Renderer CPU is 81.58% of one core, executor CPU 65.24%;
fence waits are 0.0228 ms/flip. The screenshot/readback window is excluded.

This confirms recovery from the reported single-digit/teen-rate behavior
with executor=1 and UI disabled. It does not isolate the executor from the
private UI markers, quantify an FPS speedup for identical race workloads,
measure actual presentation pacing, or validate native-resolution UI itself.
The game remains at runtime executor=1, ui_native=0 and state cache enabled.
No other graphics/debug settings were changed during recovery. Both debug
properties are temporary Android properties and may need reapplying after a
device reboot. Recovery logs, screenshot, exact selected windows and computed
metrics are saved alongside the before sample in recovery-summary.json.
