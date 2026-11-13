# Android device-bus helper optimization

13 November 2026. Branch `codex/devbus-o2` from develop `cbc913a`.
The user-tested paired APU filter was first committed as `db95e2a` and merged
into develop. Its algorithm and default remain unchanged in this feature.

## Evidence and change

The latest installed APU-filter race profile attributes 11.985% of APU
thread self samples to `xbox_ContiguousIsPhysical` (Poco 24095PCADG,
APU TID 28332). Its matching library is build ID
`5d9e216ec6c56e55fd9e19b7beb1dad627889c68`; the profile and source attribution
are retained in `performance-results/2026-11-13/apu-hotpath/candidate/`.
This is a sampled share, not predicted savings. The prior profile also
identified the same helper at 11.64% of APU samples.

`xbox_kernel` already sets O2, but the separate `xbox_devbus` library did not
inherit those private options. Its actual Android Debug compile command had
no `-O` flag. The helper checks every APU physical access against the kernel's
published contiguous-arena bounds. It is also shared with other device models.

The production change adds **`target_compile_options(xbox_devbus PRIVATE -O2)`
under `if(ANDROID)`**, applying to the complete small translation unit:
range publication/classification, IRQ bits and lazy IRQ event creation.
`xbox_devbus.c` itself is byte-identical to the baseline. Other targets and
non-Android build policy are unchanged. There is no new runtime selector,
cache, allocation, lock, scheduling change or audio algorithm.

The real APK compile database confirms exactly `-O2` for this source.
Disassembly changes `xbox_ContiguousIsPhysical` from 21 static instructions
with stack spills to 11 without a stack frame. Volatile range reads remain
and the second bound is still read only after the lower-bound check succeeds.
Both baseline/candidate disassemblies and the full compile command are saved
under ignored `performance-results/2026-11-13/devbus-o2/`.

## Address and IRQ checks

The standalone harness compiles the **actual whole `xbox_devbus.c`** at O0
and O2 while keeping its caller at O2. Both variants pass on host ARM64 with
ASan/UBSan and on Poco ARM64:

- **201,326,832 address/mapping checks per variant**. Three representative
  ranges check every byte address in the 64 MiB physical window. Edge cases
  include empty/full/single-byte/inverted ranges, exact low/high boundaries,
  top-of-window and UINT32_MAX addresses.
- The real `apu_shim.h` memory helpers resolve masked aliases into a sparse
  low/high mapping. Distinct low/physical words verify actual pointer choice,
  byte/halfword/word reads and writes, while the other alias remains intact.
- IRQ vectors 0–31, duplicate/nonzero assertions, clearing and invalid vectors
  retain their masks and event-signaling behavior. Atomic/event stubs expose
  calls and inject a lost lazy-initialization race, checking that the losing
  candidate event is closed and the winning handle retained. This checks
  device-bus control flow, not the platform's complete ISR concurrency model.

The unused shim thread-creation adapter is excluded from this standalone
fixture to avoid its unrelated legacy function-pointer cast warning. Existing
`kernel.h` nested-comment warnings remain; production code is not altered
to suppress them.

## PCM integration

The existing production VP/DSP fixture gains an opt-in real-device-bus mode.
Its normal stub mode and prior 64 scenarios remain available. In real mode,
128 scenarios cover the same voices first through low memory, then through
the physical 0x80000000 alias. Sparse virtual mapping avoids committing a
multi-gigabyte buffer. Serialized voice RAM is read through the production
translator, alongside all bins, samples, filter state and final PCM.

Both replay binaries compile VP/DSP at the same O2 with FP contraction off
and NEON filtering enabled; **only the linked device-bus object's optimization
level differs**. Host ASan/UBSan and Poco compare 16,384 frames per variant
and per downmix policy. All serialized bytes match. Device `cmp` passes and
paired SHA-256 values are:

- Two-bin policy: `d50ed7c165588d24cf19c840ba54498e156946b8d37dbd4e2565d854f7e7817f`.
- All-bin policy: `0468a1b9469bd32e5a13af71c0b290a8ff66f7d6f73409519377d05fcf181bf3`.

Multipass input exercises register/RAM translation, filters, routing and
downmix; it is not decoder/streaming coverage. The unchanged original
scalar/paired-filter replay also passes ASan/UBSan after the fixture extension.
No game data or real microphone/audio capture is used by these tests.

## CPU measurements

Poco CPU7, only standalone processes pinned (`taskset 80`), normal device
clocks. Three pairs alternate O0/O2 order; checksums agree.
The direct-call benchmark uses 16,777,216 calls per pattern and a preceding
warmup. Ranges remain volatile and the production function is linked from
a separate object, without LTO or an inlined replacement.

| Address pattern | O0 ns/call | O2 ns/call | Paired speedup |
| --- | --- | --- | --- |
| Below range | 1.6078–1.6090 | 0.7980–0.7984 | 2.015x |
| Inside range | 2.0303–2.0661 | 1.1964–1.2022 | 1.69–1.73x |
| Mixed | 2.2889–2.3352 | 1.3622–1.3868 | 1.68–1.71x |

The full production voice/DSP benchmark uses 128 scenarios x 4096 frames,
524,288 frames per variant, with all-bin downmix and both memory aliases.
It excludes replay file I/O, setup/memory clearing, live scheduling and SDL
output. All unrelated production processing, including NEON filtering, stays
enabled in both variants.

| Pair | O0 device-bus CPU ms | O2 device-bus CPU ms |
| --- | --- | --- |
| 1 | 838.225 | 812.712 |
| 2 | 837.737 | 811.411 |
| 3 | 838.797 | 812.082 |

This replay uses **3.04–3.18% less CPU time**. These are isolated measurements,
not game-wide FPS, APU utilization, audio-underrun, power or heating claims.
The earlier live buffer-underrun observation is not declared fixed by this
compiler change.

## Build and handoff

`assembleDebug lintDebug` passes with register-block/TLS-argument generation;
lint retains zero errors and 75 existing warnings. Packaged/unstripped native
build IDs agree. The helper source matches the baseline; artifact metadata
records its source/CMake hashes and the verified compile command.

APK SHA-256: `cc087cadcdde4ebbed1623aabe6dfdf8b07cbec59d2720e01ada91445ee392bb`.
Native build ID: `ac1fab3f213a6f880469103dc09975989cda00db`.
The user installed the candidate and reports that gameplay is correct, then
authorized continuing with the next optimization. The installed APK matches
the SHA-256 above. This is gameplay feedback, not a controlled performance
comparison. No install, restart, game input or persistent property change
was performed by the agent.

This is a build-only default. Rollback is the previous develop build, or
removing this target's Android O2 option and rebuilding; there is no property
to toggle compiler optimization in a running APK.

## Reproduction

```sh
python3 tests/devbus_cpu.py --sanitize
python3 tests/devbus_cpu.py --replay --sanitize --output-dir /tmp/devbus-replay
python3 tests/devbus_cpu.py --compile-only \
  --cc "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang" \
  --output-dir /tmp/devbus-arm64
python3 tests/devbus_cpu.py --replay --compile-only \
  --cc "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang" \
  --output-dir /tmp/devbus-arm64
adb push /tmp/devbus-arm64/devbus-O0 /tmp/devbus-arm64/devbus-O2 \
  /tmp/devbus-arm64/apu-devbus-O0 /tmp/devbus-arm64/apu-devbus-O2 /data/local/tmp/
adb shell /data/local/tmp/devbus-O0
adb shell /data/local/tmp/devbus-O2
adb shell taskset 80 /data/local/tmp/devbus-O0 --bench
adb shell taskset 80 /data/local/tmp/devbus-O2 --bench
adb shell env RECOMP_APU_FILTER_NEON=1 RECOMP_APU_MIXDOWN_ALL=1 /data/local/tmp/apu-devbus-O0 /data/local/tmp/old-pcm.bin
adb shell env RECOMP_APU_FILTER_NEON=1 RECOMP_APU_MIXDOWN_ALL=1 /data/local/tmp/apu-devbus-O2 /data/local/tmp/new-pcm.bin
adb shell cmp /data/local/tmp/old-pcm.bin /data/local/tmp/new-pcm.bin
```
