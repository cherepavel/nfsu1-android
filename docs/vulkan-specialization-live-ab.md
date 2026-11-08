# Live shader specialization comparison on ASUS

2026-10-20, Zenfone 7 Pro / Adreno 650, Options → Camera. One installed APK
and game process (22171, renderer 22232) throughout the comparison. Render
scale 250%, square pixels, FXAA, saved 16x anisotropy, reflections and the
60 FPS option were retained. Native YUV and D3D fence waits remained enabled;
deferred clears remained disabled. GPU timestamps were enabled, and early
submissions remained at the diagnostic value 0 throughout this comparison.

The Android shader flags now update every 300 renderer flips and select
distinct existing pipeline keys. No shader algorithm, eligibility rule,
graphics setting or command count changed during the comparison.

`alpha_specialize` removes the uniform-driven alpha-test/discard block only
when the guest test is disabled or ALWAYS. Other alpha comparisons and clip
plane discards remain. `zero_source` permits constant fragment output only
when fixed-function blending ignores the source and no alpha/clip test can
discard. See the individual optimization documents for eligibility details.

## Same-process sequence

Five-second reports were weighted by their completed frame counts. Windows
ending less than 10 seconds after a mode marker, initial menu warmup, and
non-menu reports were excluded. All selected windows retained 17 passes,
19 full barriers and approximately 14 blits/frame.

| Alpha | Zero source | Selected window ends (MSK) | Windows | Game queue ms | Final output ms | Renderer flips/s |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| Off | Off | 23:13:21–23:13:51 | 7 | 14.794 | 2.260 | 58.58 |
| On | Off | 23:14:06–23:14:26 | 5 | 12.608 | 2.259 | 59.94 |
| On | On | 23:14:41–23:15:06 | 6 | 12.364 | 2.259 | 59.94 |
| Off | Off, reverse | 23:15:21–23:15:46 | 6 | 14.831 | 2.259 | 58.41 |
| On | On, repeat | 23:16:01–23:17:06 | 14 | 12.366 | 2.258 | 59.93 |

Both-on reduced the game queue interval about 16.6% versus reverse control.
The final FXAA/output interval stayed unchanged. The light accumulation
target moved from 1.819 ms to 1.539 ms with alpha specialization and then
1.306 ms with zero-source specialization. The pre-reflection main-target
phase moved from 4.239 ms to 3.651 ms, and the final main-target UI phase
from 3.408 ms to 2.895 ms (combined across alternating backbuffers).

The user reported overlay GPU 99% before and 89–90% with both enabled,
confirming unchanged appearance and smoothness. This supports an improvement
in this menu. It does not establish maximum GPU occupancy, an exact energy
saving, or that all original heating/high GPU usage is resolved. The car
animation and GPU power state were not locked. Renderer flips do not measure
screen scanout. Queue timestamps can include waits and perturb execution.

The final source enables both guarded specializations by default on Android;
either property can still be set to 0 for a live rollback. The built APK
passed `assembleDebug lintDebug`. The existing zero-source eligibility
regression passed with host ASan/UBSan. A final ordinary run with timestamps
disabled and the original early-submit threshold restored awaits the user's
manual installation/restart. Racing, background lifecycle and other GPUs
remain outside this validation.

Raw logs, mode markers, `summarize-live-spec.py`, and weighted results are in
ignored `performance-results/2026-10-20/asus-menu/live-spec-*` artifacts.
