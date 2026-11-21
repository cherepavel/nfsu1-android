# Android APU diagnostic peak scans

13 November 2026. Branch `codex/apu-peak-scans` from develop `24dd474`.
The user-tested device-bus O2 feature was first committed as `99c4614` and
merged into develop. This feature keeps those compiler settings.

## Evidence and change

A fresh 20-second moving-race profile on Poco 24095PCADG contains 4,790 APU
thread samples with zero lost. The matching installed library has build ID
`ac1fab3f213a6f880469103dc09975989cda00db`. Profile, library, source snapshots
and line attribution are retained under ignored
`performance-results/2026-11-13/apu-next/baseline/`.

`xbox_ContiguousIsPhysical` accounts for 4.864% of APU self samples in this
recording. The following diagnostic loops account for 6.763% together:

- Per-voice trace peak: 2.818%, baseline `apu_vp.c:1268`. The peak is calculated
  before `apu_vp_trace_voice` discovers that `RECOMP_APU_TRACE` is absent.
- Frame mix-bin peak: 3.945%, baseline lines 1503–1504. This scans all 32 bins
  x 32 samples and retains the largest absolute value for diagnostic stats.

These are sampled shares of this APU thread, not projected game-wide savings.
There is no controlled before/after live-game profile for this feature yet.

The Android default now skips the first loop whenever trace is absent.
With trace enabled, it retains the original nested `fmaxf` expression and
diagnostic formatting. Presence semantics are unchanged: even empty or `0`
values of `RECOMP_APU_TRACE` enable trace. Trace selection remains cached.

The frame peak still updates on every frame. An ARM64 NEON reduction uses
four independent accumulators, one FPCR read per frame, and comparison/select
operations matching the original NaN/zero policy. Samples remain read-only.
When FPCR flush-to-zero is enabled, it uses the scalar sample order: subnormal
values can otherwise compare equal to zero while carrying different bits.
Non-ARM64 uses scalar reduction, and non-Android retains the previous default.
No decoder, filter, resampler, envelope, mix routing or PCM output algorithm
is changed. No allocation, cache, thread or lock is introduced.

`RECOMP_APU_PEAK_OPT=0` or `debug.nfsu.apu_peak_opt=0` restores both original
scans on the next process. The environment takes precedence; unset, empty
and exact `1` enable, other nonempty values disable. Startup logs
`[APUPEAK] optimized=1 neon=1` on Android ARM64; `neon` indicates compile-time
support, while the current FPCR selects the reduction for each frame.

## Validation

The host ARM64 helper passes 80,000 exact-result comparisons with ASan/UBSan,
including randomized float bits, NaN samples/seeds, infinities, signed zero,
subnormals, zero through 64 rows, alignment, exact allocation ends, four
rounding modes and native FZ/DN controls. The forced scalar build also passes.
The selector test extracts the production implementation and checks default,
exact values, property fallback, environment precedence and cached selection.

The opt-in extension to the production VP/DSP fixture additionally serializes
mix peak, trace counters and both diagnostic strings. With actual O2 device-bus
translation and paired filtering enabled in both paths, the host sanitizer
replay compares 16,384 frames per path for each downmix policy, trace off/on.
All bytes match: samples, bins, numeric filter state, guest voice RAM, final
PCM and diagnostics. Trace presence and caching are checked separately for
absent, empty, `0` and `1` values. The existing scalar/paired-filter replay
also passes host ASan/UBSan without enabling the new fixture extension.

This multipass replay covers register access, filters, routing and downmix;
it does not exercise decoding, streaming or live audio scheduling. It is not
an audio recording or a listening test. The preceding single-accumulator
version passed the helper and complete PCM/diagnostic replay on Poco; it was
replaced after CPU measurement. The final four-accumulator version passed the
host checks and native benchmark. Poco disconnected before its final native
correctness rerun. On 14 November, the user authorized the connected ARM64
tablet 24076RP19G (Android 16) instead. The unchanged final source passed all
80,000 native helper cases, selector and trace-presence/caching checks.
Four complete original/optimized VP/DSP replay pairs passed `cmp`: 16,384
frames per path for each downmix/trace combination. The paired replay SHA-256
values, in two-bin trace-off/on then all-bin trace-off/on order, are:

- `7ae5a641ba8ee85d50f5fb1b60fa77dc0fa69aedabc2eae162f3fd0bbb75bae2`
- `dc2fb44efc7b546858ecbf64bf3c0ff50516c8bae4ea6de603aa834dad0f77ed`
- `1e8bcd3ae0657697cdbaa5480abf547c0498ef3b0edcb76daf639fc106b9f081`
- `70cbf2722b2e34a20553abfd701292b22d0d3418909c51a76b35aed17f1f0201`

Raw final results and NDK-built test binaries are retained under ignored
`performance-results/2026-11-14/apu-final/`. These are tablet correctness
results; the controlled CPU benchmarks below remain Poco measurements.

## CPU measurement

Poco CPU7, only standalone benchmark processes pinned with `taskset 80`,
normal device clocks. Each process runs 128 scenarios x 4096 frames,
524,288 voice/DSP frames, excluding setup, memory clearing and output-file
I/O. Both paths include the real frame diagnostic peak scan and use the same
filter, O2 device-bus implementation and all-bin downmix. Three pairs alternate
path order.

| Pair | Original scans, CPU ms | Optimized scans, CPU ms | Less CPU time |
| --- | --- | --- | --- |
| 1 | 2288.944 | 1855.106 | 18.95% |
| 2 | 2302.600 | 1858.566 | 19.28% |
| 3 | 2296.291 | 1857.132 | 19.13% |

The replay processes one voice per frame, so the fixed 1024-sample frame scan
occupies a different share than in a many-voice race. These results do not
predict FPS, total APU utilization, power, heating or audio underruns. Raw
paired timings are in `apu-next/checks/native-bench.txt`.

## Reproduction and handoff

```sh
python3 tests/apu_peak.py --sanitize
python3 tests/apu_peak.py --scalar --sanitize --output-dir /tmp/apu-peak-scalar
python3 tests/apu_peak.py --replay --sanitize
python3 tests/apu_filter_neon.py --replay --sanitize --output-dir /tmp/apu-filter-regression
python3 tests/apu_peak.py --compile-only \
  --cc "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang" \
  --output-dir /tmp/apu-peak-arm64
python3 tests/apu_peak.py --replay --compile-only \
  --cc "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang" \
  --output-dir /tmp/apu-peak-arm64
adb push /tmp/apu-peak-arm64/apu-peak /tmp/apu-peak-arm64/apu-peak-replay /data/local/tmp/
adb shell /data/local/tmp/apu-peak
adb shell env RECOMP_APU_PEAK_OPT=0 RECOMP_APU_FILTER_NEON=1 RECOMP_APU_MIXDOWN_ALL=1 taskset 80 /data/local/tmp/apu-peak-replay bench
adb shell env RECOMP_APU_PEAK_OPT=1 RECOMP_APU_FILTER_NEON=1 RECOMP_APU_MIXDOWN_ALL=1 taskset 80 /data/local/tmp/apu-peak-replay bench
```

`assembleDebug lintDebug` passes with register-block/TLS-argument generation;
lint retains zero errors and 75 existing warnings. Packaged and unstripped
native build IDs match, and the actual VP command retains O2 and FP
contraction off. Frozen artifact and source hashes are under
`apu-next/candidate/artifact.json`.

APK SHA-256: `183112654cdec97720d071bb997b0a2e5119ed300a51676e35cc5b249f51ebf0`.
Native build ID: `15d6e23fb03581077224ed148eec8ecdd97a5eaf`.

On 14 November the final APK was already installed and running on the tablet;
its device-side SHA-256 matches the artifact above. Startup confirms
`[APUPEAK] optimized=1 neon=1`, `[APUFILTER] neon=1`, and SDL OpenSL ES output
at 48 kHz stereo. After the native checks, the user confirmed that an ordinary
race and its sound are in order. This supplies the gameplay acceptance for
merging the feature into `develop`; it is not an agent listening test or a
controlled live-game performance comparison. No APK installation, restart,
game input or persistent property change was performed by the agent.
