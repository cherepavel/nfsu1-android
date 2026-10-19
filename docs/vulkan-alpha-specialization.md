# Experimental Android alpha-test specialization

Android can select `debug.nfsu.alpha_specialize=1` before process startup.
The default is 0. Restart the game process after changing the property.
`[VK] alpha specialization=N` confirms the selected mode. To roll back:

```sh
adb shell setprop debug.nfsu.alpha_specialize 0
adb shell am force-stop org.nfsu.android
adb shell am start -n org.nfsu.android/.LauncherActivity
```

The Vulkan fragment shader has a Boolean specialization constant (ID 0,
default true) around its existing uniform-driven alpha-test block. Android
sets this constant to false only when guest alpha testing is disabled or the
comparison is ALWAYS (0x207). Enabled tests retain their exact existing
comparison/reference logic. CLIP_PLANE discards remain outside this guard.
The constant is part of the Android pipeline key, so changing effective
alpha-test state cannot reuse the opposite variant. VkBool32 is used for the
specialization map entry size. The shader key and program-record layout are
unchanged; generated GLSL changes naturally invalidate the SPIR-V source hash.
The GL dialect output is unchanged, and non-Android Vulkan uses the default
true constant.

This lets the driver compile out a potentially fragment-discarding path on
opaque draws. It does not force early fragment tests on alpha-tested geometry.
Whether the measured benefit comes specifically from early depth rejection
requires shader/driver counters; the FPS result alone does not prove that.

References: [Vulkan specialization constants sample](https://docs.vulkan.org/samples/latest/samples/performance/specialization_constants/README.html),
[VkSpecializationMapEntry requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkSpecializationMapEntry.html).

## Checks

- Root `./gradlew assembleDebug -Pnfsu.genOpt=-O2`: successful; APK installed
  on ASUS ZS671KS (Adreno 650).
- Temporary host comparison of 512 generated shader sources against HEAD:
  the legacy body is identical after removing the constant/guard, GL output
  is identical, and CLIP_PLANE discards precede the alpha guard.
- Actual menu startup and shader compilation succeeded with the toggle on
  and off. GPU timestamp profiling remained disabled during FPS comparisons.
- Long races, cross-device behavior, Vulkan validation layers and independent
  presentation FPS are not validated. SurfaceFlinger latency returned zero
  timestamps for the queried surfaces, so renderer flip counters are used.

Raw logs, source-comparison harness and the rejected attachment-layout patch
are local ignored artifacts in `performance-results/2026-10-19/asus-alpha/`
and `performance-results/2026-10-19/asus-layout/`. The attachment-layout
experiment did not improve menu FPS and was removed from the maintained code.

## ASUS menu comparison, 2026-10-19

Same installed O2 debug APK, settings 250%, reflections/FXAA/16x/square pixels
on, experimental 60 FPS. GPU profiling off throughout. Three sequential
30-second windows, six complete PERF5 records each, after loading:

| Mode | Log record timestamps, MSK | Renderer flips/s | Host present ms/flip |
| --- | --- | ---: | ---: |
| On, first | 22:22:55.768–22:23:20.809 | 55.86 | 5.16 |
| Off, reverse control | 22:24:18.416–22:24:43.442 | 50.68 | 7.95 |
| On, repeat | 22:25:43.354–22:26:08.385 | 55.73 | 6.38 |

The repeat is +9.95% versus the reverse control. These are renderer flip
counts, not independently measured displayed frames. MSK timestamps identify
window ends; each record covers the preceding roughly five seconds. The
menu's car/camera animation, Android scheduling and GPU power state were not
locked. This supports a repeatable benefit in this menu, not a universal
10% gain or a race-stutter fix. `asus-alpha/summarize.py` and `summary.json`
retain the exact calculation and selected records. GPU profiling and the
rejected layout property are off; alpha specialization is left on on the ASUS.
