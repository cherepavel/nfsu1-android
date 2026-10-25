# Native ARM64 matrix multiplication

The CPU profile on Poco 24095PCADG found `sub_00169F4A` (4×4 matrix multiply)
at 5.02% self CPU time in a race. The installed generated `-O1` implementation
expanded packed SSE arithmetic into scalar loops and repeatedly materialized
guest XMM registers in TLS. Its ARM64 body contained 843 static instructions.

`src/recomp_manual.c` now wraps this function. On Android ARM64,
`src/matrix_native.c` uses the NEON implementation in `matrix_native.h`,
compiled separately at `-O2 -ffp-contract=off -fno-strict-aliasing`. It keeps
the function-entry safe point, all guest-visible GP/XMM results, the original
multiply/add order, and `ret 12` stack cleanup. Inputs are read before output
stores, so matrix overlap and output overlapping the guest stack remain valid.
The final return value is reloaded from the guest stack after output stores,
matching the original even when the output overwrites its argument slot.

The fast path accepts ordinary low guest RAM only. MMIO, wrapped/out-of-range
addresses, an unaligned stack, and matrices containing NaN/infinity use the
unchanged generated body. NaN payload selection in the scalar compiler output
can differ from SIMD code; keeping the original fallback avoids changing it.
The generic SSE helpers, scheduler and Vulkan renderer are unchanged.

The completed native function, including Android property/TLS handling and
guards, contains 190 static instructions and 28 packed multiply/add
instructions, with no FMA. Instruction counts describe code size, not the
number of instructions executed by every invocation.

## Validation, 2026-10-26

- Differential test against the actual generated function: 36,000 cases on
  macOS ARM64 with AddressSanitizer/UBSan and on the physical Poco with NDK30.
  Covers all GP/XMM outputs, full touched RAM, exact/partial/stack overlap,
  unaligned matrices, random raw float bits, FP exception flags, four rounding
  modes and rejection without state changes for invalid/MMIO pointers.
- Existing manual scanner: 5 checks passed. Manual call dispatch: 9 checks
  passed, including direct/tail calls through wrapped functions.
- `./gradlew assembleDebug lintDebug`: BUILD SUCCESSFUL.
- Installed and launched the APK on Poco; the saved Underground menu renders.
  A 12-second live CPU profile contains `nfsu_matrix_native` samples, confirming
  that the application uses the new implementation. The generated fallback
  also remains reachable. This menu is not the earlier race workload.

An isolated Poco test, three alternating pairs of one million matrices:

| Path | CPU time |
|---|---:|
| Scalar generated reference compiled at O2 | 80.27–80.94 ms |
| Native NEON kernel with RAM/non-finite guards | 19.64–19.73 ms |

This is approximately 4.1× for the isolated kernel. It excludes the production
Android wrapper/property sampling, contention and game frame work. It does
not establish an FPS, GPU load or temperature improvement. Other Android ARM64
devices use the same code without a GPU-vendor dependency, but their speedup
and stability have not been measured. Long races and cross-device checks remain.

The APK SHA256 is
`ac0c74293739ff51c3b8c633baf8bc7b16ff35f4ca8fda7b258ec577b9063a7f`.
Raw captures are in the ignored `performance-results/2026-10-26/device-profile/`
directory (`native-matrix-bench.txt`, `native-matrix-disassembly.txt`,
`native-live-symbols-all.txt`).

## Reproduce

```sh
python3 tests/matrix_native.py --sanitize --output /tmp/nfsu-matrix-test
/tmp/nfsu-matrix-test
python3 tests/matrix_native.py --cc /path/to/aarch64-linux-android30-clang \
  --output /tmp/nfsu-matrix-test-android
adb push /tmp/nfsu-matrix-test-android /data/local/tmp/nfsu-matrix-test
adb shell /data/local/tmp/nfsu-matrix-test bench
```

For live return/repeat comparisons, `debug.nfsu.native_matrix=0` uses the
generated implementation; `1` or an empty value enables NEON. Each host guest
thread checks the property once per 65,536 calls. The default is enabled.

## Remaining native-path work

This converts one measured CPU hotspot, not the entire Xbox compatibility
layer. TLS/safe-point overhead, CPU preparation of Vulkan draw commands and
audio mixing remain separate measured costs. Bulk replacement of packed SSE
helpers needs its own float-state and NaN audit; it is not enabled here.
Vulkan shaders already execute through compiled SPIR-V pipelines, and image
blits/presentation already use native Vulkan commands. Translation of Xbox
state and resource formats is still required at that boundary; removing it
without preserving those semantics would change the game.
