# Android adaptive widescreen

This mode is enabled by default and fills the upright Android game surface at
its actual aspect ratio. Physical screen diagonal is not used. The user confirmed
correct widescreen behavior on a device before the default was enabled.

To restore the previous 16:9 presentation, set this before fully restarting:

```sh
adb shell setprop debug.nfsu.adaptive_wide 0
```

Alternatively, add `RECOMP_ADAPTIVE_WIDESCREEN=0` to the existing
`nfsu1x_env.txt`. The environment entry takes precedence over the property;
unset or empty values use the enabled default. Explicit `0` and invalid
nonempty values disable the mode; exact `1` enables it. `RECOMP_HORPLUS=0` or
`RECOMP_WIDESCREEN=0` also disables it. Clearing the property/environment
entry or setting it to `1`, then restarting, restores adaptive behavior.
Saves and graphics options do not need to change.

## World, racing HUD and menus

The presenter uses the entire logical output rectangle, without adding side
or top/bottom bars. Java surface callbacks publish the upright dimensions;
successful swapchain creation also publishes its logical extent, rather than
the physical pre-rotated image extent. Width and height are one atomic snapshot.
An unavailable surface retains the last valid size. Resizing updates the
projection on subsequent game frames without restarting.

Feature policy lives in `src/widescreen.c`: startup configuration, mode
selection, camera projection and HUD/menu layout. `src/widescreen_guest.c`
adapts these decisions to guest memory and scopes. `recomp_manual.c` retains
only thin entry points and generated-body declarations required by the
recompiler scanner. Backend output sizing remains in the shared
`video_layout.c` helper.

A 4:3 output selects **native mode**: camera
projection, HUD positions and garage angle remain original. Wider outputs
select **adaptive mode**. Resizing back to 4:3 restores native behavior in
the same process. Disabling adaptive widescreen selects the previous 16:9 mode
for comparison; the existing Hor+/widescreen rollback flags still apply.

Racing and garage cameras retain the current camera's vertical FOV and adapt horizontal
FOV: `hFOV = 2 * atan(aspect * tan(vFOV / 2))`. Instead of multiplying the
original horizontal scale by a fixed 3/4, the hook derives its magnitude as
`abs(m11) / aspect`. This also removes the distortion from the game's
approximate conversion between horizontal and vertical FOV. Projection signs,
near/far depth terms and camera FOV changes are retained. The correction runs
before view/projection multiplication and frustum extraction, so culling sees
the expanded view. It applies to the existing 4:3 main/copy views; square
environment-map faces retain their original projection. Garage views now add
space at the sides instead of zooming the car as aspect increases. At the
same camera distance and output height, its projected height stays constant.
This replaces the first experiment's fixed-width garage policy. Check the
newly exposed sides of the garage set during device validation.

The **racing HUD** keeps proportional gauges, minimap, icons and text. Its
virtual canvas is `480 * aspect` wide. Horizontal projection is
`(4/3) / aspect`; left/right groups shift by half the additional canvas width.
The minimap and complete cached text/sprite objects receive the same dynamic
edge anchoring. Centred notifications remain centred, and cached geometry is
restored after each draw.

**Every FEng menu stretches as a whole**, including text, background, bevels
and buttons. This includes the main menus, pause, race results, retry,
continue tournament, finish race and claim reward screens. These remain menus
even when game flow is still racing (state 6): the existing FEng menu draw
scope, rather than game flow alone, identifies them. Every menu quad undoes
the HUD projection equally; it does not receive a HUD edge shift. Loading
menus keep their original uncorrected projection and fill the output too.
Touch mapping inverts this same full output rectangle and the Menu Size
slider, including regions previously occupied by the side bars.

The optional output-resolution UI path shares the present rectangle helper.
It remains independently opt-in and keeps its existing no-upscaling guard.
Render resolution, reflections, filtering and FXAA settings are unchanged.

## Validation

Host regressions cover legacy/rollback and 4:3, 16:10, 16:9, 18:9, 20:9,
21:9, ultrawide and odd output dimensions. Actual guest wrappers are exercised
with synthetic draws, including unchanged source geometry, glyph grouping,
minimap anchoring, racing HUD scope restoration, menu projection cancellation,
3D focal-scale ratios, depth terms, square-target exclusions and guest ABI.
The display-state test also checks concurrent publication of width/height;
the native-UI mock checks full output dimensions and resizing. ASan/UBSan
checks pass. These do not establish visual correctness on a device.

The separate policy regression covers enabled defaults, empty/invalid values,
explicit-off rollback, configuration precedence, native 4:3,
wide garage/race projection and switching back to native on resize. Guest
tests also check unchanged native camera angle and constant projected object
height as the display widens. Manual-scanner entry points, generated bodies
and protected references are unchanged by the extraction.

On 12 November 2026, `assembleDebug lintDebug` passed with the normal
register-block/executor-TLS flags. APK signature and 16 KB alignment checks
passed. The APK was not installed or started on a device by the agent.

Build the APK with the established `./build_debug.sh`. At startup, look for
`[WIDE] adaptive=1 requested=1`, then a renderer line such as
`[WIDE] adaptive=1 output=2400x1080 aspect=2.222222`. Compare both modes on the
same camera and graphics settings. Check car/wheel proportions, track edges,
speedometer/minimap/leaderboard placement, pause and each end-of-race action,
touch targets near both edges, and resume/reverse landscape.

Reproduce the focused host checks on macOS (use `--gc-sections` instead of
`-dead_strip` on Linux):

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/video_layout.c xboxrecomp/src/kernel/video_layout.c \
  -pthread -lm -o /tmp/nfsu-video-layout
/tmp/nfsu-video-layout
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/widescreen.c src/widescreen.c xboxrecomp/src/kernel/video_layout.c \
  -lm -o /tmp/nfsu-widescreen-policy
/tmp/nfsu-widescreen-policy
cc -std=gnu11 -O1 -fsanitize=address,undefined \
  -ffunction-sections -fdata-sections \
  -Iandroid/app/build/generated/recomp -Ixboxrecomp/include \
  tests/ui_aspect.c src/recomp_manual.c src/widescreen.c src/widescreen_guest.c \
  xboxrecomp/src/kernel/video_layout.c \
  -Wl,-dead_strip -lm -o /tmp/nfsu-ui-aspect
/tmp/nfsu-ui-aspect
/tmp/nfsu-ui-aspect 4:3
/tmp/nfsu-ui-aspect adaptive 1024 768
/tmp/nfsu-ui-aspect adaptive 2400 1080
RECOMP_HORPLUS=0 /tmp/nfsu-ui-aspect adaptive
```
