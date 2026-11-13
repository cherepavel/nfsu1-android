# Android manual wrapper O2 candidate

14 November 2026. Branch `codex/manual-wrappers-o2` from `develop` at
`5842802`, after the validated APU peak feature was merged.

## Scope and evidence

The [fresh tablet profile](android-cpu-profile-2026-11-14.md) attributes
177 samples / 1.26% of executor CPU to `src/recomp_manual.c`. The actual
Android Debug compile command had no optimization level. This is a small
candidate relative to the renderer and remaining executor checks.

Android now compiles this translation unit with
`-O2 -fno-strict-aliasing -fwrapv -ffp-contract=off`, preserving the generated
code's guest-memory aliasing, signed-wrap and separate FP-operation contract.
Its source and algorithms are unchanged. APU core, SDL, zlog, glslang,
generated code, runtime scheduling and renderer compilation settings are
unchanged. No fast-math, new cache, thread or runtime selector is introduced.
Rollback is removal of this Android-only source option and an APK rebuild.

## Validation

`tests/manual_cpu.py` runs seven production wrapper fixtures at O0 and O2,
with separate TLS registers and with the actual register-block/executor-argument
ABI: 28 combinations. Six existing fixtures now share register definitions so
their assertions can run with both ABIs; their scenario assertions are retained.
The additional native-wrapper fixture links `recomp_manual.c` as a separate
translation unit and checks culling/matrix/movie fallback, register/stack
effects, contention and preemption order/owner argument.

All 28 combinations pass host ARM64 ASan/UBSan and native execution on the
user-authorized tablet 24076RP19G / Android 16. Existing fixtures cover GPU
flip accounting across IRQ interleavings/wrap, Blur/Bloom persistence and
restoration, credits fallback/stack handling, controller settings guards,
lookback scanners and raw sample restoration, and race input capabilities
and release. Host Android property declarations and the diagnostic thread-ID
syscall name are adapted only for compilation of unused platform code; native
tests use the NDK headers. No tested wrapper algorithm is replaced.

These checks are not exhaustive equivalence of all manual functions, x87/SSE
instructions or a running game. The native math/conversion implementations
called by the thin wrappers are unchanged. No controlled CPU benchmark or
FPS gain is claimed for this feature yet.

```sh
python3 tests/manual_cpu.py --sanitize
python3 tests/manual_cpu.py --compile-only \
  --cc "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang" \
  --output-dir /tmp/nfsu-manual-arm64
```

Push the compiled files to a temporary device directory and run each
`<fixture>-<legacy|block>-O<0|2>` binary there. Raw host/native results and
the exact native execution script are retained under ignored
`performance-results/2026-11-14/manual-o2/checks/`.

`assembleDebug lintDebug` passes with register-block/TLS-argument generation:
zero lint errors, 76 warnings. The actual compile database confirms all four
new flags only for the manual wrapper source; APU core remains without `-O`.
Packaged/unstripped native build IDs match. Frozen libraries, source hashes
and compile commands are in `manual-o2/candidate/artifact.json`.

APK: `android/app/build/outputs/apk/debug/app-debug.apk`.

- SHA-256: `06085907ae6ec94362a3036631fa3435134630cb81a0951849446ce45be4aac4`.
- Native build ID: `e648ef8a051a0d5d60745da07631cdb27f6d705e`.

The candidate is ready for the user's APK installation and ordinary race,
audio, menu and control checks before merge. The agent has not installed or
restarted this candidate or changed persistent settings. Gameplay feedback
for the preceding APU APK does not validate this new APK.
