# Android audio CPU optimization

2026-10-26. Logging and diagnostic settings are preserved.

The race CPU profile showed the APU thread near 32% of one core. The installed
Android Debug build compiled `apu_vp.c` and `apu_dsp.c` without an optimization
level, unlike the kernel and Vulkan renderer, which already use O2. VP frame
processing accounted for about 10% inclusive sampled process CPU; final mixdown
also appeared in the profile (1.65% self in the later menu capture).

Android now compiles these two translation units at `-O2 -ffp-contract=off`.
No fast-math, FMA, resampling/filter replacement, thread scheduling or logging
change is introduced. Audio-device code and the rest of the APU retain their
previous compilation settings.

## Correctness

The production voice and mixdown source files are replayed directly by
`tests/apu_vp_cpu.c`, compiled at O0 and O2. The deterministic fixture covers
64 scenarios and 128 frames each: 3D/2D voices, mono/stereo routing, low-pass
filters on/off, envelope progression, monitor routing and mix clearing. Both
two-bin and all-bin final mixdown policies run in separate processes.

On macOS ARM64, AddressSanitizer and UBSan initially found signed-left-shift
undefined behavior in APU register masks. Five masks now use unsigned literals,
preserving their intended bit patterns. The subsequent replay passes both
sanitizers with `-fno-sanitize-recover=all`.

On physical Poco 24095PCADG, NDK30 ARM64 O0/O2 outputs are byte-identical for
each mixdown policy: samples, final PCM buffers, numeric filter state, relative
filter output pointers and guest voice RAM. Raw process pointers are excluded
because ASLR changes their addresses. There are 8192 voice/mixdown frames per
policy. The fixture uses multipass voices as deterministic sample input; it
does not validate PCM/ADPCM decoding, streaming/resampling or all game audio.

Reproduce on the host:

```sh
python3 tests/apu_vp_cpu.py --output-dir /tmp/nfsu-apu --sanitize --run
```

For Android, use the NDK compiler with `--cc` and omit `--run`. Push `apu-O0`
and `apu-O2` to the device. Each accepts an output path for replay comparison,
or `bench` for a replay without output-file I/O. Set
`RECOMP_APU_MIXDOWN_ALL=0` and `1` in separate processes when comparing files.

## Performance

Poco, three alternating pairs of 262144 production voice/mixdown frames,
all-bin policy, without output-file I/O:

| Run | O0 CPU ms | O2 CPU ms | Ratio |
|---|---:|---:|---:|
| 1 | 2354.235 | 643.168 | 3.66× |
| 2 | 2349.720 | 649.410 | 3.62× |
| 3 | 2319.153 | 642.223 | 3.61× |

This is an isolated replay, not an end-to-end game FPS, battery or thermal
measurement. CPU governor state was not locked. The audio algorithms, input
and compilation architecture are the same in each pair.

An intermediate VP-only APK was also observed in the game. Separate 15-second
APU-thread samples recorded 3.269 s task-clock / 8.241 billion instructions
before and 2.750 s / 3.470 billion afterwards. Voice workload, scene and CPU
frequency differed; this is not a controlled estimate of savings. Final
VP+mixdown performance claims are limited to the reproducible replay above.

`./gradlew assembleDebug lintDebug` passes. The audio-only checkpoint APK was
installed and launched on Poco (guest code still at O1).
SHA256: `5a12b07e062eef1530361e4f64d4ee2ba5660b83da4826931a4fa26d19f12bfd`.
Raw outputs and measurements are in ignored
`performance-results/2026-10-26/apu-cpu/`.

The main guest and renderer CPU threads remain separate hotspots. This change
does not establish that overall CPU utilization or heating is resolved.

The final audio-only live sample used 3.557 seconds of task-clock and 3.502
billion instructions in 15.001 seconds. This exceeds the earlier task-clock
samples despite executing fewer instructions than the original APK: scenes,
voice counts and clocks changed. These live samples do not establish a
reduction in overall CPU usage. The controlled replay is the performance
evidence for the audio change.

## Generated game CPU code

The same CPU pass changes the normal Android generated-code default from O1
to O2 in Gradle and CMake, retaining `-fno-strict-aliasing` and `-fwrapv` and
explicitly disabling FP contraction. The existing `-Pnfsu.genOpt=-O1` override
remains available for return/repeat measurements. Other desktop defaults are
unchanged. No scheduler, TLS memory model or logging algorithm is changed.

The SSE/x87 generator checks pass: 36 tests run, 35 pass, one x86-only runtime
comparison skipped on ARM64. This is not exhaustive validation of every
generated game function. Whole-game CPU/FPS and long-race behavior require
separate validation and must not be inferred from the audio replay speedup.

The final combined Android O2 APK builds with lint successfully and is installed
on Poco. Generated `recomp_0032.c` compile flags are verified as
`-O2 -fno-strict-aliasing -fwrapv -ffp-contract=off`. The regenerated matrix
reference/native comparison also passes 36,000 cases on the device.
Final APK SHA256:
`3d3a1588602780f79c35103ed1cad3c46e330b1fda07ca0814712030ec912520`.

A separate O1/O2 main-thread observation recorded 13.543 vs 10.788 seconds of
CPU time over 15 seconds (90.3% vs 71.9% of one core), and 41.164 vs 28.501
billion instructions. Scenes, loading and clocks differed; this is an
observation, not an established 20% CPU or 31% instruction improvement.
The O2 capture still contains a 68 ms texture-loading hitch. Heating and
long-session stability have not been measured.
