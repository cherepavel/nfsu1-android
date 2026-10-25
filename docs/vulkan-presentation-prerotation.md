# Android presentation rotation and GPU composition

2026-10-21, ASUS Zenfone 7 Pro / Adreno 650. Installed return/repeat testing
confirms removal of a redundant system GPU composition pass. The overall
heat/high-GPU issue is not declared solved. The final build enables the checked
path by default and awaits manual signing/installation.

## Evidence outside the game queue

The four descriptor comparison windows report client composition on every
display frame. A separate same-process capture then isolates Game Genie:

| HUD state | Full SF dump MSK | Display/client frames in following ~15 s window | Reported RenderEngine timing |
| --- | --- | ---: | ---: |
| On | 13:51:09 | 902 / 902 | 1.580 ms |
| Off | 13:52:56 | 901 / 901 | 1.768 ms |

Both captures use PID 20905, dynamic_desc/native_push=0 and the same Camera
scene/settings. The user disabled only the floating monitor. Screenshots
confirm reflections, 2.5x, FXAA, 16x anisotropy, Square Pixels, 60 FPS and
Blur/Bloom off in both. The timing difference is not evidence of a HUD speedup
or regression: clocks are uncontrolled and this is a system interval metric.

The **actual game SurfaceView**, with and without the HUD, is:

```
composition=CLIENT
bufferTransform=ROT_90
sourceCrop=2400x1080
displayFrame=1080x2400
forceClientComposition=false
```

The other visible window/decor layers use DEVICE composition. The HUD is not
necessary for the game's client-composition path. Debug properties forcing
client composition or disabling HWC were unset; the global disable_hw_overlays
setting is absent. No system compositor settings were changed.

The Android swapchain code explicitly selects `preTransform=IDENTITY` when
supported, while producing upright landscape pixels. This leaves output
rotation to the presentation system. Its precise `currentTransform`/selected
flags were only DEBUG-level before this experiment; new INFO diagnostics expose
them. The SF dump proves a rotated client layer, but does not alone prove the
driver's reason for rejecting hardware composition. Confirm that via A/B.

[Android's pre-rotation guide](https://developer.android.com/games/optimize/vulkan-prerotation?hl=en)
and [Khronos's surface-rotation sample](https://docs.vulkan.org/samples/latest/samples/performance/surface_rotation/README.html)
explain why compositor-side rotation can add a system pass and memory traffic.
Their performance examples concern other devices, not expected gains here.

## Correction, enabled by default after installed validation

Startup-only `debug.nfsu.prerotate=1`, or an unset property, requests actual
pre-rotation. Explicit 0 retains the old path; invalid nonempty values also
disable it. The installed test APK originally required explicit 1; the final
build changes only this default policy. The swapchain selects a supported pure current rotation
(identity/90/180/270), using swapped physical dimensions for quarter turns.
For this landscape surface, expect logical 2400x1080 and buffer 1080x2400 if
the current rotation is a quarter turn. Guest targets, resolution, passes,
textures, draws, anisotropy and FXAA parameters are unchanged.

The existing final FXAA pass rotates its viewport/scissor and maps physical
pixel centres back to the original logical pixel centres **before** the original
UV calculation. Texture sampling and FXAA edge searches stay in source axes.
No additional rotation pass or intermediate image is introduced. Letterboxing
remains logical and is rotated as an exact pixel rectangle. The push block
grows from 32 to 40 bytes for the logical full extent and uses its previously
unused component for rotation/copy flags.

With FXAA off and rotation active, the same final shader performs a linear
copy with no FXAA; it cannot use a non-rotating transfer blit. In the default
unrotated mode, FXAA-off retains the original blit. Headless FXAA dumps always
use zero rotation. Mirror/unsupported transforms retain legacy behavior.
Failed rotated swapchain creation disables the experiment for this process
and returns to legacy creation (an existing retired chain is released by the
normal rebuild loop). An unavailable final pipeline/view clears one frame and
requests legacy rebuilding, rather than drawing an unrotated blit into the
rotated buffer. Existing surface-generation/size handling is retained;
SUBOPTIMAL in rotated mode checks caps at most every 30 frames and rebuilds
when the transform/extent changed.

`[VKROT] requested=… failed=… current=… selected=… rotation=… logical=… buffer=…`
records the applied state at every successful creation. Restart is required.

## Validation

- Host ASan/UBSan: 304,128 pixel/rectangle membership checks, all quarter turns,
  small/odd dimensions and asymmetric letterboxes pass.
- The **same GLSL coordinate helper used in production**, run headlessly on
  Adreno 650, produces bit-identical normalized UVs for 4,194,304 cases across
  four rotations, logical extents and asymmetric rectangles: zero failures.
- Actual vertex/fragment source strings are extracted and compile to Vulkan
  1.1 SPIR-V with NDK glslc. Removing only the coordinate-remap/copy additions
  reproduces the previous fragment shader body exactly; the FXAA algorithm is
  unchanged. These checks do not establish full rasterized image equivalence,
  WSI rotation direction or absence of runtime driver failures.
- `./gradlew assembleDebug lintDebug` passes; APK stays at the fixed debug path.

## Installed return/repeat result

All runs use the same manually installed test APK, Camera, full saved quality,
native_push/dynamic_desc/perf/gpu_profile=0 and kick=300. Each startup's actual
VKROT state was checked. A1/B1 have the HUD off. The user re-enabled it before
A2, so A2/B2 form a second pair with the HUD on; this is not a four-window test
with one fixed HUD state. Screenshots and SF layer dumps preserve both states.

| Run / prerotate | PID / renderer TID | Stat start MSK | HUD | Display frames | Client compositions | Game FPS, approximate | RenderEngine metric |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: |
| A1 / 0 | 22803 / 22855 | 14:09:05 | Off | 900 | 900 | 59.72 | 1.738 ms |
| B1 / 1 | 23214 / 23266 | 14:11:56 | Off | 1201 | **0** | 59.84 | 0.000 ms |
| A2 / 0 | 23622 / 23672 | 14:14:51 | On | 1202 | 1201 | 59.84 | 1.681 ms |
| B2 / 1 | 24765 / 24820 | 14:18:01 | On | 1202 | **0** | 59.88 | 0.000 ms |

A1's SF window is 15.037 seconds; the others are 20.036–20.039 seconds.
Game-layer frame counts divided by display-on time establish approximate
cadence; windows slightly exceed the requested duration and are not precise
game CPU denominators. Game-layer droppedFrames=0 in all four windows.

Legacy startup: `current=2 selected=1 rotation=0`, logical/buffer 2400x1080;
the game layer is CLIENT + ROT_90. New startup:
`current=2 selected=2 rotation=1 logical=2400x1080 buffer=1080x2400`;
the **actual game layer becomes DEVICE with transform 0**. The complete
frame windows then contain zero client compositions, with and without the HUD.
This is an observed removal of external GPU composition, not an inference
from fewer source lines, passes inside the game, or a changed WSI flag alone.
The RenderEngine metric must not be equated to a fixed amount of pure shader
time or subtracted mechanically from the game queue interval.

The user confirmed unchanged picture, letterboxing, controls and no new
artifacts/flicker. At 14:19:39 Home/resume kept PID 24765 and restored the
rotated buffer. A physical flip to reverse landscape at 14:20:13 selected
`current=8 selected=8 rotation=3`; the user confirmed correct orientation,
and the game remained DEVICE/transform 0. Returning at 14:23:09 restored
current/selected=2, rotation=1, again DEVICE/transform 0. No continuous rebuild
loop or fallback failure appears in these captured VKROT markers.

After those checks the source default was enabled; host default/explicit-off
tests, ASan/UBSan and `./gradlew assembleDebug lintDebug` pass. The installed
APK is still the opt-in test build, kept active by prerotate=1. The new APK at
the fixed output path carries the default-on policy; it is not installed by
the agent. Other unsuccessful experiments remain off. SF TimeStats is disabled
after capture; all logs, raw results and `prerotation-abab-summary.json` are saved.

This fixes one confirmed source of extra GPU work while preserving quality.
It does **not** establish the overall reduction in GPU energy or resolve the
remaining load/heat: late screenshots still show roughly 87–89% Game Genie GPU
and 46–47 C, with a transient 77% immediately after resume. Those are neither
frequency-normalized utilization nor a controlled thermal A/B. No GPU clock
or power samples were obtained here. Sustained GPU/thermal behavior and other
scenes, including FXAA-off presentation, still need broader validation.

Raw evidence, coordinate probe, build and validation records are ignored under
`performance-results/2026-10-21/asus-menu/{composition-*,prerotation-*}`.
