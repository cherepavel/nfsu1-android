# Blur/Bloom Off and the later frame-effects stage

The main frame loop invokes world rendering (`16050`), then the separately
labelled `Frame Effects` stage (`15370` -> `28FF0`). The latter returns early
when native DWORD `21C0A8` is zero; its original XBE value is 1.

Previously the Blur/Bloom option only zeroed speed-blur request/smoothing/cap
(`260CC0`, `264FA4`, `21C0B4`) around world rendering. It did not disable the
later bloom chain. Active menu render-target snapshots with the option Off
still showed dark blurred car/effect buffers, including a 2133x1200 target.
This identified a setting inconsistency, not proof that bloom explains all
menu GPU utilization.

The world-render wrapper now captures the native bloom flag on the first Off
frame and holds it at zero across the later effects call. On re-enabling the
option it restores the captured value, including an originally disabled flag.
Repeated Off frames do not overwrite that snapshot with zero. Existing
speed-blur and car-reflection behavior is preserved.

## Verification

`tests/options_blur.c` includes the actual maintained wrapper and checks
persistence, both reflection modes, disable across the return boundary,
repeated Off frames, and exact native-value restoration (including zero).
Both ordinary and `-DNFSU2_VULKAN` host runs pass:

```sh
cc -O1 -DNFSU2_VULKAN -ffunction-sections -fdata-sections \
  -Isrc -Ixboxrecomp/include -Iandroid/app/build/generated/recomp \
  tests/options_blur.c src/input_mode.c -Wl,--gc-sections -lm \
  -o /tmp/nfsu-options-blur-test
/tmp/nfsu-options-blur-test
```

Root `./gradlew assembleDebug -Pnfsu.genOpt=-O2` succeeded and the APK was
installed on ASUS ZS671KS. The runtime log confirmed `disabling native bloom (saved=1)`. In the same
user-reported menu, the overlay moved from 99% GPU to 92-94%. Final renderer
windows showed 300 flips per approximately 5005 ms (about 59.94 flips/s).
Settings remained 250%, reflections/FXAA/16x/square pixels on, Blur/Bloom Off.
This is a single interactive session and an overlay utilization report, not
an independent GPU occupancy measurement. Some pre-fix windows already reached
60 flips/s, so no FPS increase is attributed to this fix. Live On/Off visual
restoration, long races and cross-device testing remain unverified.
Raw artifacts are under local ignored `performance-results/2026-10-19/asus-bloom/`.

## One-shot target snapshots

Android's diagnostic `debug.nfsu.dump_targets=1` captures active color targets
once per process at the next 300-frame boundary into `logs/target-*.bmp`.
Restore the property to 0 afterward. This readback intentionally waits for
the GPU and writes images synchronously; its frame and timing window must
not be used as performance samples. It does not represent a rendering policy.
