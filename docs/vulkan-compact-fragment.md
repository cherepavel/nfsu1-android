# Experimental compact single-texture fragment shader

Android property `debug.nfsu.compact_psh=1` opts into an alternate fragment
module for one exact NV2A state. Unset/0 keeps the generic module. It is polled
with the other specialization properties every 300 renderer flips. Pipelines
include the mode in their key, so switching needs no app restart. The first
activation compiles modules/pipelines; exclude warm-up windows from timing.
A failed alternate compilation retains the generic module and logs the failure.

The accepted state is one combiner stage, texture mode 1 on T0 and NONE on T1–3,
color ICW/OCW `08040000/000100c0`, alpha ICW/OCW `18143030/000100c0`, final
`13010c00/00001c00`, control 1 and other_input 0. Inactive combiner stage slots
are ignored. Every bit in the active words is checked conservatively. This
matches 26 of 55 historical program records (22 distinct fragment keys) pulled
from this phone; cache frequency is not evidence of execution frequency.

The compact body keeps projective UV division/epsilon, texture scale, texture
sampling, max/product/scale/clamps, vertex color, fog, and final saturation in
the same order. It omits unused register/intermediate calculations. Interfaces,
precision, descriptors, uniforms, alpha/discard tail and the two existing
specialization constants are shared with the generic generator. Shader source
shrinks from 3070 to 1870 bytes, including declarations and alpha-test code.
Source size is not a measurement of GPU instructions or speed: the driver may
already eliminate the unused calculations.

The alternate module is compiled lazily and retired with its owning program.
Frame/pass profiling reports `compact_eligible_per_frame` and
`compact_selected_per_frame` beside actual indexed Vulkan draws. Eligibility
counts submitted draws with the accepted shader state; selected counts actual
alternate modules (not failed attempts). An instanced command is counted once,
with its held shader metadata carried until flush. This maps shader coverage to
render-target dimensions and pass end phase, without additional GPU queries.
Profiling stays opt-in. These counters measure coverage, not cost per shader.

## Validation on 2026-10-20

- `tests/vulkan_compact_psh.c`: host ASan/UBSan passes; every active-state bit
  mutation rejects, inactive stages preserve the generic source, common
  interfaces and alpha tail match, unsupported GL path rejects, output capacity
  boundaries are checked.
- The generic source remains byte-identical to the prior generator for all
  55 cached program keys in both GL and Vulkan modes.
- Both generated fragment sources compile through the bundled glslang to
  Vulkan 1.1/SPIR-V 1.3.
- `tests/vulkan_compact_psh_gpu.py` extracts the actual generated function
  bodies and produces a compute differential test. Texture results are replaced
  with identical synthetic texels; it tests combiner/fog/clamp/alpha semantics,
  not filtering, rasterization, projective derivatives or performance.
- `tests/vulkan_compute_check.c` ran that SPIR-V on the connected Adreno 650:
  **4,194,304 cases, zero mismatches**. Cases include finite values outside
  [0,1], clamp edges, signed zero, tiny/large values, all alpha comparisons plus
  unknown/pass, and alpha/zero-source toggles. Numeric comparison treats signed
  zeros as equal.
- `assembleDebug lintDebug` passes. The subsequent live comparison below
  confirms shader coverage and unchanged appearance, but negligible speedup.
  The experimental mode remains disabled by default.

Reproduce the shader checks:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/vulkan_compact_psh.c xboxrecomp/src/nv2a_gl/gl_psh.c \
  -o /tmp/nfsu-compact-psh-test
/tmp/nfsu-compact-psh-test /tmp/nfsu-generic.frag /tmp/nfsu-compact.frag
python3 tests/vulkan_compact_psh_gpu.py /tmp/nfsu-generic.frag \
  /tmp/nfsu-compact.frag > /tmp/nfsu-compact-compare.comp
```

Build glslang with ENABLE_OPT=OFF, matching the app, and compile all three
sources with `-V --target-env vulkan1.1` and the corresponding frag/comp stage.
Cross-compile the compute runner with the NDK (`-std=c11 -O2 -lvulkan`), push
it and compute SPIR-V to `/data/local/tmp`, and pass the SPIR-V path as its sole
argument. The runner creates a headless compute queue; it does not install or
modify the game.

## Live menu comparison

The user installed the APK manually and opened the same Options → Camera menu.
One process (PID 32348, renderer 32488), alpha/zero-source specializations on,
GPU timestamps on, early-submit threshold 0, deferred clears off. Live modes
were alternated 0→1→0→1; shader/pipeline compilation happened only on first
activation. Summaries exclude the first 10 seconds after each mode marker,
initial scene loading, and windows with a pass count other than 17. Values are
weighted by completed frames. Resolution remains 2133×1200 for the main target,
with the same saved quality settings.

| Compact | Stable windows | Frames | Game queue ms | Final output ms | UI ms |
|---|---:|---:|---:|---:|---:|
| 0 | 10 | 3000 | 12.371 | 2.258 | 2.890 |
| 1 | 6 | 1800 | 12.323 | 2.260 | 2.873 |
| 0 reverse | 12 | 3600 | 12.355 | 2.259 | 2.895 |
| 1 repeat | 6 | 1797 | 12.320 | 2.258 | 2.873 |

Live counters confirm the alternate modules actually replace all eligible
submitted draws: approximately 210–211 pre-cube draws/frame, all 202 UI draws,
and all 7 draws on target 82A4ED80. Three alternate programs compiled
successfully. Alternating backbuffer contributions are summed here, not treated
as two scene renders per frame.

The observed total difference is only about 0.03–0.05 ms (0.3–0.4%), with a
roughly 0.02 ms UI difference. The user reported GPU still 90% and unchanged
appearance/smoothness. This does **not** resolve the original high load or
support a significant performance claim. Animation and GPU frequency were not
locked, and queue timestamps include bubbles. The result is consistent with
the driver's eliminating most unused generic operations already; it does not
prove identical driver machine code.

The experiment stays off by default and `debug.nfsu.compact_psh` was restored
to 0 after collection. Properties for timestamps and early submits were
restored to 0 and 300 respectively; those two startup policies take effect on
the next manual restart, not immediately in the profiled process. Existing
alpha/zero-source improvements remain enabled.

Raw logs, mode markers, `summarize-compact.py` and `compact-live-summary.json`
are local ignored artifacts under `performance-results/2026-10-20/asus-menu/`.
