# Android bulk vertex-shader constant packets

12 November 2026. Branch `codex/bulk-shader-constants`, based on `develop`
`e488f3d`, after the user-tested executor query change was merged (`8a2dae9`).

## Target and implementation

The last pre-runtime race profile identifies scanner/command execution as
16.820% of renderer CPU self samples: `nv2a_pb_scan` 5.607%,
`nv2a_pb_exec_method` 5.920%, and `nv2a_pb_exec_run` 5.293%. These distinct
self shares can be added; they do not measure the work's possible speedup.
The capture is described in [the runtime report](android-runtime-leaf-query.md).
It predates this constant change and the executor-query optimization.

The scanner already dispatches whole incrementing vertex-program constant
packets to `nv2a_pb_exec_run`. However, that routine still looped over every
dword, checking/storing the shadow register, writing its dirty byte, checking
the constant-vector bound, copying four bytes and advancing the load index
on each fourth component. This change targets that loop; it does not replace
the entire scanner or method executor.

Android now processes packets of **16–32 dwords** as follows:

- Update the shadow in 16-dword dirty blocks. ARM64 loads, XOR-compares and
  stores four words at a time with NEON, then reduces the differences. A dirty
  byte is set to exactly 1 only when a word in its block changed. A portable
  helper follows the same rules. No read extends past the packet end.
- Copy the contiguous valid constant bytes together rather than by component.
  The method supplies only the starting component; the existing LOAD index
  still selects the destination vector. Float payloads are copied as bytes,
  preserving NaNs, signed zero and other bit patterns.
- Advance LOAD by the number of completed vectors, with the same unsigned
  wrap behavior. Out-of-range LOAD discards writes until it wraps; a packet
  crossing UINT32_MAX still writes its wrapped tail from constant zero.

Shorter packets retain the old scalar loop. The initial microbenchmark found
that the four-word bulk case was slower, so the final path uses a conservative
16-word threshold. Every accepted packet increments `gen` and `const_gen`
**once**, including identical data and out-of-range writes, just as the
previous packet path did. Program generation and vertex-cache dependencies
are preserved. This is not an identical-constant suppression cache.

The incrementing method-range guard is now subtraction based, rejecting
overflowing unsupported inputs before reading source memory. Valid decoded
packets retain their previous eligibility. Indices, other commands, single-word
dispatch, verbose/survey paths and draw submission keep their previous paths.

Source command memory is disjoint from the private shadow/constants. Processing
is synchronous on the renderer's command-executor thread; no backend observes
the intermediate word stores within a packet. The helper changes no guest
memory, shader source, descriptor layout, upload-ring lifetime or Vulkan work.

## Selection

Default on for Android. `debug.nfsu.const_bulk=0` restores the previous scalar
constant packet loop. Exact `1`, empty and unset enable the new path; invalid
nonempty values disable it. `RECOMP_PB_CONST_BULK`, if present, takes precedence,
including an empty value. Selection is cached at the first eligible large
constant packet; fully restart after changing it. Startup of this path logs
`[PBCONST] bulk=1` or `0`. Short packets need no selector/property read.

Other platforms retain the scalar packet path. No per-packet clocks,
allocations, locks or additional periodic counters were added.

## Correctness and build validation

The fixture compiles the actual production packet routine and VP-state
declaration extracted by the test runner. It compares the retained scalar
mode with the enabled hybrid mode, including the complete shadow, dirty bytes,
constant/program arrays, LOAD, all generation fields and unrelated state.

- **301826 cases pass** on host ARM64 with ASan/UBSan, for both NEON and forced
  portable helper builds, and on Poco with the NDK30 ARM64 build. There are
  85680 valid large-packet comparisons that take the new helper path.
- Every start method/component and valid count, four source alignments, normal
  and boundary LOAD values, UINT32_MAX wrap, equal/changed/mixed float payloads,
  noncanonical existing dirty bytes, generation wrap, rejected packets,
  verbose bypass and index packet coexistence are covered. Source allocations
  end at the last valid dword so ASan checks vector overreads.
- Separate-process startup cases cover default/empty/exact/invalid overrides,
  environment precedence and cached selection.
- Final `assembleDebug lintDebug` succeeds with grouped registers and executor
  arguments enabled. Existing compiler/lint warnings remain. Packaged and
  unstripped native library build IDs match; disassembly confirms NEON shadow
  comparisons and reduction in the final APK.

## Isolated CPU measurement

Algorithm measurements are saved locally under
`performance-results/2026-11-12/bulk-shader-constants/`. The final measurement
also links the **actual `nv2a_pb_exec.c.o` from the APK build** into a standalone
probe. A copy of that object has only its private VP/shadow/selector data symbols
globalized so the probe can set LOAD and choose the warmed scalar/bulk cache
value. Its packet function and helper object code are unchanged, with normal
relocations for the standalone link. The probe's
logging stubs are not called within the timed warmed path. The APK itself is
not modified by this extraction.

Three pairs alternate path order, with 3 million packets per path/scenario.
Cases include 4, 8, 16 and 32 words, start slots 0 and 15 where valid, stable
payloads and a changing first word. Shifted packets cross a shadow dirty-block
boundary and start at component 3. CPU time includes resetting LOAD, the
changing-word increment and the actual packet call. It excludes process/setup,
GPU work and game rendering. Normal device clocks are not locked.

Poco 24095PCADG, ARM64, NDK30 production object:

| Packet | Scalar, ns/packet | Enabled, ns/packet | Speedup range across pairs |
| --- | ---: | ---: | ---: |
| 16 words, start slot 0 | 56.722–60.410 | 25.650–25.893 | 2.20–2.34x |
| 16 words, start slot 15 | 56.507–57.901 | 30.515–30.928 | 1.85–1.90x |
| 32 words, start slot 0 | 101.486–104.751 | 33.048–33.213 | 3.06–3.17x |

Both modes use the same scalar loop for 4/8-word packets; their small timing
differences are not treated as a speedup. Checksums and the final register
and constant payloads match in every benchmark invocation. These numbers
measure processing of a constant packet, not all command parsing.

The user reports that gameplay looks correct and subjectively feels smoother
with less heating. This is a successful visual gameplay check and subjective
feedback, without a measured temperature, FPS or power comparison.
APK SHA256: `edcaaffd59661fb630b6768df955f08006faa27b30ea280ee65cc6a30bdac0a0`.
Native build ID: `1441e40da969f70b3f4c6111681d1ecb2ffe3875`.

## Reproduction

```sh
python3 tests/nv2a_const_upload.py --sanitize
python3 tests/nv2a_const_upload.py --sanitize --scalar \
  --output-dir /tmp/nfsu-const-portable
python3 tests/nv2a_const_upload.py \
  --cc "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang" \
  --compile-only --output-dir /tmp/nfsu-const-android
adb push /tmp/nfsu-const-android/nv2a-const-upload /data/local/tmp/
adb shell /data/local/tmp/nv2a-const-upload
adb shell /data/local/tmp/nv2a-const-upload --bench
```

After building the APK, append `--production-object` pointing to its CMake
`nv2a_pb_exec.c.o` and `--objcopy` pointing to NDK `llvm-objcopy`. This also builds
`nv2a-const-packet-bench`; push it and run it without arguments for the
production-code measurement. No game installation, restart, input or properties
are changed by either probe.
