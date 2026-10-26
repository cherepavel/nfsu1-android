# Grouped TLS register experiment

The optional `NFSU2_REGISTER_BLOCK` build mode stores guest GP, x87, MMX and
XMM registers in one native-thread-local `RecompRegisterBlock`. It defaults
to OFF. Android builds select it with `-Pnfsu.registerBlock=true`; use false
for the original separate-symbol storage.

The generated `g_*` accesses address fields in that block. The generator
temporarily removes aliases for registers already localized in a function,
then restores them for subsequent functions. GPR/MMX locals, the x87 stack
pointer and top index retain their existing spill/reload behavior. Helpers
defined in the header still address real TLS storage. Fiber switching saves
and restores architectural values as before. The executor `running` pointer,
scheduler, logging and dynamic TLS model are unchanged.

## Verification

Both storage modes passed Android ARM64 context, fiber, executor and real
kernel-executor regressions, including 2000 yields, native thread ownership,
longjmp, FP state, kernel waits and recursive locks. Both also passed the
36,000-case matrix comparison against generated scalar code (registers,
RAM/stack overlap, raw float bits, FP flags and four rounding modes).

The new compiled generator test covers localized GPR/MMX/x87 state,
nonlocalized XMM state, helpers bypassing locals and alias restoration for
the next function. The focused test run passed all 12 tests. The full
generator suite reported 456 passed, 11 failed and one skipped. Repeating
with the original translator from Git produced the identical 11 failures;
these are not evidence that the full suite is green.

`assembleDebug` and `lintDebug` passed. Experimental APK SHA256:
`389aebbbe750ede9a503850693b43d9970a6fb56b9db6466798b0cae66298bd7`.
Original comparison APK SHA256:
`8fe07fba926a0885fb3fcee3d03e2217f14074b7e869e31ae109fb7f2ee5918f`.

## ARM64 code

Static indirect-call instruction counts in the selected complete functions:

| Function | Separate registers | Grouped registers |
|---|---:|---:|
| `sub_001B2344` | 9 | 1 |
| `sub_00018C30_gen` | 11 | 1 |
| `sub_001D4790` | 19 | 1 |
| `nfsu_matrix_native` | 12 | 4 |
| `recomp_context_switch` | 35 | 4 |

These are static `blr` counts from disassembly, not dynamic call counts or
predicted speedups. The matrix wrapper retains its property-polling TLS.
Assembly and APKs are saved locally under
`performance-results/2026-10-26/register-block/{before,after}/`.

## Shared-library matrix microbenchmark

On the original phone (26 October), alternate dlopen-loaded O2 PIC libraries
call the actual `nfsu_matrix_native` wrapper five million times. Input matrices
are finite, fixed and disjoint; every iteration resets the stack register.
The test checks the result against scalar matrix multiplication and verifies
the call count and final guest registers. Thread CPU time:

| Pair | Separate CPU ms | Grouped CPU ms |
|---|---:|---:|
| 1 | 240.534 | 128.571 |
| 2 | 240.913 | 127.022 |
| 3 | 242.602 | 128.021 |

About 47% less CPU time for this synthetic call loop. Clocks were not locked.
This is not whole-game CPU, FPS, power or heat evidence. Source, binaries,
outputs and regression/build logs are in the same local results directory.

## ASUS follow-up, 27 October

The connected device changed to ASUS ZS671KS; the user authorized testing
there. Its installed APK matches the experimental SHA256 above. Treat ASUS
runs as a separate comparison from the original phone. Native culling was
set to 0 and performance counters enabled before launching the measurement
run. Other game settings were retained, including scale=250, fps=60,
reflections=1, blur=0, fxaa=1 and aniso=16. Normal logging remains enabled.

Two 25-second CPU-clock/1000 Hz executor recordings were collected, grouped
first and separate-register baseline second. Installed APK hashes were
verified for both. The user drove ordinary races rather than repeating a
fixed scene. Both recordings lost zero samples (18,067 grouped; 24,182
baseline). Four complete PERF5 windows wholly inside each recording give:

| Metric | Separate baseline | Grouped block |
|---|---:|---:|
| Executor CPU, % of one core | 94.70 | 71.10 |
| Renderer CPU, % of one core | 90.03 | 94.63 |
| APU CPU, % of one core | 19.60 | 17.23 |
| Renderer flips/s | 41.13 | 42.25 |
| Dynamic TLS resolver self, % executor | 18.41 | 9.11 |
| Resolver attributed to generated `sub_*`, % executor | 12.24 | 3.00 |
| Resolver attributed to `xgf_preempt_status`, % executor | 4.67 | 5.22 |
| Resolver attributed to native matrix, % executor | 0.88 | 0.27 |

Recording intervals were 00:47:38–00:48:03 (grouped, TID 28113) and
00:49:57–00:50:22 (baseline, TID 31268), Moscow time. CPU-clock sample
percentages and PERF5 CPU percentages have different denominators. The TLS
caller categories partition resolver self time; do not add them to inclusive
safe-point time. `xgf_preempt_status` remains the largest TLS caller in the
grouped profile, with 57.29% of all dynamic resolver samples.

These runs show the expected change in where resolver time is spent, supported
by the assembly and shared-library microbenchmark. The observed executor CPU
is 23.60 percentage points lower in the grouped run, but different driving
scenes, run order and temperature prevent treating that number as an isolated
effect of the patch. Renderer load also differs. No reliable FPS, power or
heat improvement is established. Battery temperature after capture was
34.0 C grouped and 36.2 C baseline; the screenshot's overlay uses a different
temperature reading and must not be mixed with these battery values.

Raw profiles, recorded screens, complete logs and caller/counter JSON are under
`performance-results/2026-10-27/asus-register-block/{block,legacy}/`.
The user now handles builds, installs and game launch; profiling starts only
after their readiness message. No automatic installation or restart is pending.

### Repeat grouped run

After the baseline, the user rebuilt with `-Pnfsu.registerBlock=true`, installed
and launched the race. The installed APK matched the local rebuilt package,
SHA256 `ca0a3c077b0fecf588508dccc2de923a5190ac52fe940f7be4e8c1eb19ef1e50`.
Its unstripped native library is byte-identical to the first grouped build
(SHA256 `033dcbff722ca2c0dc6237dcdf72850807d86ff6dca9662776d4de7a1f578dbf`).
Executor/performance counters remained 1 and native culling 0.

The 00:53:32–00:53:57 recording on executor TID 3216 collected 19,652 samples
with zero losses. Four complete PERF5 windows inside that interval show:

| Metric | Repeat grouped |
|---|---:|
| Executor CPU, % one core | 77.47 |
| Renderer CPU, % one core | 82.43 |
| APU CPU, % one core | 20.39 |
| Renderer flips/s | 44.50 |
| Dynamic TLS resolver self, % executor | 9.00 |
| Resolver attributed to generated `sub_*`, % executor | 2.67 |
| Resolver attributed to `xgf_preempt_status`, % executor | 5.39 |
| Resolver attributed to native matrix, % executor | 0.27 |

Battery temperature after capture was 37.7 C. The reduction in sampled TLS
share recurred after the baseline (18.41% baseline versus 9.11% and 9.00%
grouped). Executor CPU was also lower in both grouped runs, but driving scenes
and renderer load differ, so the observed CPU/FPS differences remain exploratory
rather than a precise isolated speedup. This does not establish lower power or
temperature. In the repeat, the unchanged safe-point executor-pointer lookup
accounts for 59.95% of remaining dynamic resolver samples and is the next
concentrated TLS target.

Raw repeat artifacts are in `asus-register-block/block-repeat/` alongside the
other two runs. TLS caller and site period totals were checked against resolver
totals; PERF5 summaries include only windows wholly inside each recording.
