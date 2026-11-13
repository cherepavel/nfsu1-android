# Android paired-channel APU low-pass filter

13 November 2026. Branch `codex/apu-hotpath` from `develop` at `2a08743`.
The preceding user-tested paletted texture feature was committed as `a0f5a04`
and merged into develop before this branch was created.

## Fresh race profile

The user resumed racing on Poco 24095PCADG / Mali-G615 MC2. Simpleperf
CPU-clock at 1000 Hz recorded all game-process threads for 20 seconds:
52,085 samples, zero lost. The APU thread (TID 14309, game PID 12290)
accounts for 6858 samples, about 6.858 sampled CPU seconds / 20.072 seconds,
or roughly **34% of one core** in this run.

The installed baseline is the last tested texture APK,
SHA-256 `63f495727c0162303ea14523226bfb5a0dec55375451363e113542875f924dcf`.
The saved unstripped library and recorded native build ID match
`86255fc30cbd705368e8ea3b701c24dc48877385`. Race screenshots, source copies,
profile, matching symbols, thread report and source attribution are under
ignored `performance-results/2026-11-13/apu-hotpath/baseline/`.

In the APU thread, `mcpx_apu_vp_frame` includes 78.90% of samples;
`run_svf` equations account for 11.07% self samples and their following
clamp for 2.90%, about **13.97% of APU CPU** together. The per-channel
sample recurrence prevents normal loop vectorization across samples.
`xbox_ContiguousIsPhysical` separately accounts for 11.64%; its device-bus
translation unit is still compiled without `-O`. Resampling, address
translation, trace-peak scans and remaining mixing costs are separate future
candidates. Those shares are sampled costs, not predicted savings.

## Implementation

The low-pass filter runs the same state-variable equations on left/right
channels in the two lanes of ARM64 NEON. Consecutive samples still execute
in order; every channel retains its own coefficients and recursive state.
Mono voices retain the existing duplicated-channel processing and coefficient
selection. Existing low-pass enable/mode checks, volume, envelopes, resampling,
HRTF, mix routing, downmix and output limiter are retained.

Both scalar coefficient setups run before the paired block. The arithmetic
sequence within each channel remains multiply/subtract/add in the original
order. Existing `-O2 -ffp-contract=off` stays in place; no fast-math, FMA,
reassociation, approximation or quality change is introduced. The helper
stores final `b/l/h/n/p` fields and leaves coefficients and the `op` pointer
intact. It specializes the actual one-iteration low-pass (`F_R == 1`), with
a compile-time assertion requiring review if that algorithm changes.

An early test found different NaN payloads in recursive state because the
scalar compiler can commute operands. The final helper checks all input
samples plus used coefficients/state for nonfinite values using integer
exponent bits. It returns before any output/state write if such a value is
present; the caller then executes the original scalar filter. Unused initial
`h/n/p` do not need checks because the first iteration overwrites them.
Finite inputs, including subnormals, stay eligible. Both arithmetic paths
use the thread's native FP mode; no float-result cache is used.

On Android ARM64 the paired path defaults on. Explicit rollback and
unsupported builds retain scalar processing. Non-Android policy stays scalar;
the standalone replay uses a test-only opt-in to exercise both policies on
the ARM64 host. Processing remains in the existing APU thread under its
existing lock. No new allocation, worker thread, queue, pacing or scheduling
change is introduced.

## Selection and coverage

`debug.nfsu.apu_filter_neon=0` restores the original scalar filter.
Unset, empty and exact `1` select paired filtering when available;
other nonempty values select scalar. `RECOMP_APU_FILTER_NEON`, when present,
takes precedence, including empty. Fully restart after changing selection.
The candidate needs no override. Startup records `[APUFILTER] neon=1`.

While enabled, every 15,000 VP frames (about ten seconds at 48 kHz / 32)
the performance log reports `paired_blocks` and `fallback_blocks` since the
last report. Each counted block is one low-pass call for 32 stereo samples;
non-low-pass, paused and muted voices that return earlier are not counted.
The counters have APU-thread ownership. There are no per-sample counters,
clocks or repeated property queries.

## Correctness evidence

- The unit oracle extracts the real `sv_filter`, setup and scalar recurrence
  from `apu_state.h`. The production header is compared with the original
  channel-major scalar processing, including clamp and every numeric state
  field. Self-relative output pointers are also checked.
- Host ARM64 ASan/UBSan and Poco ARM64 pass **40,000 blocks**: independent
  coefficients/states, random finite and raw-bit samples, lengths
  0/1/2/3/7/31/32/33/64, four rounding modes and native FPCR FZ/DN controls.
  Nonfinite rejection is checked with a signaling NaN at the end of a block,
  requiring unchanged input/state; a finite block must take NEON successfully.
  Zero-length processing does not dereference either pointer.
- Forced-scalar host checks pass ASan/UBSan. Ten separate-process selector
  cases cover capability, defaults, rollback, exact invalid values,
  environment precedence and cached selection.
- The existing production VP/DSP replay runs at O2 with paired mode 0/1:
  64 scenarios x 128 frames per mode/downmix policy. It covers mono/stereo,
  3D/2D routing, low-pass modes, envelopes and monitor routing. Samples,
  all mix bins, numeric filter states, voice RAM and final int16 PCM are
  byte-identical on host ASan/UBSan and Poco for both downmix policies.
  The replay uses multipass input; decoder/streaming coverage is outside its
  scope and those paths are unchanged by this feature.
- Poco `cmp` verifies full replay files, with matching SHA-256 per policy:
  two-bin `826097c21b7e341c69da38a9dc04652d41599cc5e7123404ef27c753676b0594`,
  all-bin `9d5ddc61e1e76a470fee56ff50f2562dc9d2f8bb1d854bbc1343e29ebdb6ce2c`.
- `assembleDebug lintDebug` passes with register-block/TLS-argument codegen;
  lint has zero errors and the existing 75 warnings. Packaged/unstripped
  native build IDs match; packaged selector and log marker are verified.

## CPU measurements

Only standalone test processes are pinned to CPU7 (`taskset 80`). Device
clocks, game state and properties are not changed. Three pairs alternate
scalar/paired order; measurements are thread/process CPU time, not wall time.

The kernel benchmark processes 262,144 blocks of 32 stereo samples per mode,
including finite checks, copying samples, periodic state reset and a checksum.
Paired checksums match. This excludes coefficient setup, voice processing,
Vulkan and audio output:

| Pair | Scalar ns/block | NEON ns/block |
| --- | --- | --- |
| 1 | 1265.075 | 471.465 |
| 2 | 1263.974 | 471.163 |
| 3 | 1259.682 | 471.107 |

The isolated kernel is about **2.67–2.68x faster** on this Poco.

The full production voice/DSP replay benchmark uses all-bin downmix, 64
scenarios x 4096 frames per mode, without replay file I/O. It includes
filter selection/counters and unchanged non-filter work; it does not include
the live APU scheduler, SDL output or the entire game:

| Pair | Scalar CPU ms | Paired CPU ms |
| --- | --- | --- |
| 1 | 657.674 | 601.522 |
| 2 | 658.345 | 602.085 |
| 3 | 656.190 | 601.940 |

CPU time falls **8.27–8.55% in this replay**, about 1.09x throughput.
This is not a measured game-wide APU/FPS/power or heating improvement.
Subsequent user feedback and live inspection are recorded below. No game
install/restart, input or property change was made by the agent.

Raw results are `host-filter.txt`, `host-scalar.txt`, `host-replay.txt`,
`device-filter.txt`, `device-replay.txt`, `benchmark-filter.txt` and
`benchmark-vp.txt` under the ignored results directory. `artifact.json`
records exact source hashes and the APK identity:
SHA-256 `dcad3b5101a6b608a2cf022f48b047b07a87a4ebb184f00fcc849c9a94845a7a`,
native build ID `5d9e216ec6c56e55fd9e19b7beb1dad627889c68`.

## Live candidate inspection

The user installed/started the candidate, reports that it seems correct and
requested inspection. Installed APK SHA-256 matches the candidate above.
Startup confirms `neon=1`, SDL OpenSL ES, 48 kHz stereo, 1024-sample device
buffer and an eight-block queue. Screens show racing and subsequently a
loading transition. Game PID is 14591, APU TID 28332.

Ten complete reports from 15:40:34.411 through 15:42:04.406 (device log time)
cover 150,000 VP frames / about 100 seconds of generated audio:
**2,575,915 paired blocks, zero scalar fallback blocks**, or 82,429,280 stereo
sample frames processed by NEON. These are LPF calls, not unique output frames
summed across all voices. `candidate/counter-summary.json` retains the totals.

A new 20-second APU-only CPU-clock profile has 4539 samples, with no loss,
matching candidate symbols. Sampled CPU is about 4.539 seconds / 20.114 seconds,
**22.6% of one core in this interval**. Paired-filter source lines account for
8.13% of APU self samples. Different scenes/voices, clocks and possible loading
transitions prevent interpreting this against the earlier 34% as a controlled
performance gain. The isolated replay remains the comparison evidence.

AudioFlinger track 9372 / session 51337 belongs to the same game PID and remains
active, unmuted, with 512 ready frames in both active-track snapshots. Its
underrun counter increases from 0 to **1024 frames** while the server position
advances by 6,206,208 frames (about 129.296 seconds at 48 kHz). The dump column
reports underrun frames, as shown by the [AOSP track dump implementation](https://android.googlesource.com/platform/frameworks/av/+/9a375d710b/services/audioflinger/Tracks.cpp).
The increase is a cumulative equivalent of **21.33 ms**, without establishing
the number of audible interruptions or a connection to the new filter.
The interval includes profiling and later a loading transition.

The game subsequently loses its window/foreground at 15:45:49.870 and is no
longer running when an internal SDL queue-stat read is attempted. Further
AudioFlinger underruns accumulated during track removal are excluded from
the active-track comparison. There is no new native crash-log entry (the
file's last modification is 26 October) and the PID-filtered Android crash
buffer is empty. These checks do not establish the process's exit cause.

No live audio was recorded/listened to by the agent: capture tools are absent
and the built-in PCM dump is startup-only. Internal SDL underrun counters
were not read before process exit. Thus subjective sound quality is based on
the user's feedback; system underrun observations must not be described as
zero-dropout audio or as a filter regression. Raw logs, snapshots, current
profile and `audio-observation.json` are retained in the ignored results
directory's `candidate/` subdirectory. No playback setting was changed.

## Reproduction

```sh
python3 tests/apu_filter_neon.py --sanitize
python3 tests/apu_filter_neon.py --scalar --sanitize --output-dir /tmp/apu-filter-scalar
python3 tests/apu_filter_neon.py --replay --sanitize --output-dir /tmp/apu-filter-replay
python3 tests/apu_filter_neon.py --compile-only \
  --cc "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang" \
  --output-dir /tmp/apu-filter-arm64
python3 tests/apu_filter_neon.py --replay --compile-only \
  --cc "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang" \
  --output-dir /tmp/apu-filter-arm64
adb push /tmp/apu-filter-arm64/apu-filter /tmp/apu-filter-arm64/apu-filter-replay /data/local/tmp/
adb shell /data/local/tmp/apu-filter
adb shell taskset 80 /data/local/tmp/apu-filter --bench
adb shell env RECOMP_APU_FILTER_NEON=0 RECOMP_APU_MIXDOWN_ALL=1 /data/local/tmp/apu-filter-replay /data/local/tmp/scalar.bin
adb shell env RECOMP_APU_FILTER_NEON=1 RECOMP_APU_MIXDOWN_ALL=1 /data/local/tmp/apu-filter-replay /data/local/tmp/paired.bin
adb shell cmp /data/local/tmp/scalar.bin /data/local/tmp/paired.bin
adb shell env RECOMP_APU_FILTER_NEON=1 RECOMP_APU_MIXDOWN_ALL=1 taskset 80 /data/local/tmp/apu-filter-replay bench
```
