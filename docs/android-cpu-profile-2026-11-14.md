# Android CPU profile after APU peak scans

14 November 2026. The APU peak feature passed native bit comparisons on the
user-authorized ARM64 tablet 24076RP19G / Android 16, and the user confirmed
ordinary racing and sound. It was committed as `41824ca` and merged into
`develop` as `5842802` before the next feature was started.

## Recording identity

The tablet was already running the final APU APK:

- APK SHA-256: `183112654cdec97720d071bb997b0a2e5119ed300a51676e35cc5b249f51ebf0`.
- Native build ID: `15d6e23fb03581077224ed148eec8ecdd97a5eaf`.
- Game PID 8433; executor TID 6487, renderer 6490, APU 6497.
- Startup confirms APU peak/filter NEON enabled. The recording does not install,
  restart, drive, change properties or lower saved graphics settings.

NDK simpleperf records all threads of the game process with CPU-clock,
1000 Hz and callchains. Matching unstripped symbols, binary cache, source
attribution, screenshots, metadata and logs are retained under ignored
`performance-results/2026-11-14/cpu-profile/`. This is an Android Debug build
on the tablet; earlier Poco recordings are separate observations.

## Moving race

The user resumed driving for the requested 20-second recording. It contains
52,072 samples, zero lost/truncated samples, and 52.072 sampled CPU seconds
across all threads. The first/last sample span is 20.200 seconds. The screenshot
after recording shows an active lap and changed position.

| Thread | Sampled CPU seconds | CPU as % of one core over sample span |
| --- | ---: | ---: |
| Renderer 6490 | 19.240 | 95.25 |
| Executor 6487 | 14.009 | 69.35 |
| APU 6497 | 6.000 | 29.70 |
| Mali worker 6521 | 3.733 | 18.48 |

Renderer self shares: `vk_draw_raw_impl` 10.54%, `tex_get` 8.32%,
`nv2a_pb_exec_method` 4.72%, `nv2a_pb_scan` 3.10%, `draw_primitive` 2.86%.
The Mali DSO accounts for 42.46% of renderer samples. Many driver addresses
are unnamed, and the report's nearby exported conversion symbol must not be
interpreted as proof that all of those samples perform float conversion.
`android_push_descriptors` is 21.81% inclusive; this overlaps its callees and
must not be added to self shares. Per-draw preparation and host driver calls
remain strong candidates for the next substantial renderer feature.

Executor self shares: `xgf_preempt_status_for_executor` 6.34%, dynamic TLS
resolver 5.97%, `recomp_preempt_status` 2.13%, and
`recomp_preempt_for_executor` 2.01%. TLS caller attribution is saved in
`race/executor-tls.json`; it includes 837 resolver samples with no missing
caller attribution. Generated callers remain distributed across functions.

APU self shares: VP 62.40%, device-bus translation 5.47%, `expm1f` 3.67%,
DSP 3.47%, `tanhf` 2.70%, envelope stepping 2.27%, and `powf` 1.60%.
Paired-filter source accounts for 10.48% of APU samples. These identify
follow-up investigation sites, not the savings from caching coefficients.

## Track transition

The initial 30-second follow-up (`loading/`, 78,486 samples, zero loss)
started after the user had launched another race. Its screenshot already
shows driving, so it is not treated as a complete loading profile.

The second recording starts before the next user-triggered transition and
runs for 60 seconds (`loading-full/`, 137,128 samples, zero loss). It includes
previous racing, menu navigation, loading and the new race. Its whole-recording
averages are deliberately not reported as pure loading costs.

One-second CPU windows show the transition near relative seconds 29–34,
followed by sustained racing from about second 34. The screenshot during the
recording shows a different car/track and a newly started lap. Selecting
29–34 seconds gives 8,541 samples: executor 3.036 CPU seconds, renderer 1.602,
GPU acknowledgement thread 6491 1.319, and APU 0.733. This is a transition
window inferred from the recording and screenshots, not instrumented exact
load boundaries. Executor `syscall` has 24.47% self in this window;
renderer DXT source has 34 samples / 34 ms sampled CPU. Neither observation
establishes the cause or duration of an individual long frame.

This capture does not provide per-frame CPU/GPU timing or reliable separate
wall totals for DXT, layout conversion, image creation and upload. The detailed
timing counters were not enabled for this process, and normal graphics logs
do not contain the DEBUG hitch/stage breakdown. A later texture feature needs
those stage measurements before choosing its implementation. Current samples
do not justify declaring DXT or image creation the dominant loading cost.

## Compiler audit and next feature

The actual APK compilation database confirms that `recomp_manual.c`,
`apu_core.c`, `apu_mmio_hook.c` and `apu_xaudio2.c` lack an optimization level.
VP and DSP already use O2 with FP contraction off. SDL has 185 source entries
without `-O`, zlog 47, and glslang 48; compilation state alone does not assign
their performance priority.

In the moving race, source attribution gives `recomp_manual.c` 177 samples /
1.26% of executor CPU, and `apu_core.c` 99 samples / 1.65% of APU CPU. These
are modest measured shares. The next isolated feature tests O2 only for
manual wrappers, retaining the guest memory/integer/FP contracts. APU core
and third-party compilation settings remain separate candidates. No FPS,
power, temperature or whole-game speedup is established by these profiles.
