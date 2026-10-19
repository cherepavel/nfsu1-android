# Experimental zero-source fragment specialization

Android opt-in: `debug.nfsu.zero_source=1`, then restart the game process.
The default is 0; rollback is `setprop debug.nfsu.zero_source 0` and restart.
The startup log reports `zero source specialization=N`.

In the confirmed ASUS car menu, target 82A4ED80 (2133x1200) has seven draws:
a full-image fade with source factor ZERO and destination CONSTANT_COLOR,
followed by six additive textured light sprites. The shared fragment program
still samples a texture and computes color for the fade, although blending
ignores that source value.

Vulkan specialization constant ID 1 (default false) enables an early constant
zero output only for source-independent blending. Eligibility requires:

- Blending enabled, source factor ZERO, operation ADD/SUBTRACT/REVERSE_SUBTRACT.
- Destination factor depending only on destination or blend constants.
- Guest alpha testing disabled or ALWAYS.
- No CLIP_PLANE texture stage, which can discard fragments.

MIN/MAX (which ignore factors), source-dependent destination factors, alpha
comparisons and clipping retain the original shader. Vertex processing,
geometry, depth/stencil, scissor, output masks and blending are unchanged.
The specialization is part of the Android pipeline key; the default fragment
path and GL output retain their prior behavior. No graphics option is lowered.

## Checks and observations

- `tests/vulkan_zero_source.c`: accepted factor/op combinations, rejected
  source dependence, alpha comparisons, MIN/MAX and all four clip stages pass.
- 512 generated shader sources match the original body after removing the
  opt-in guards; GL source output is identical.
- Root `./gradlew assembleDebug -Pnfsu.genOpt=-O2` succeeds; APK installed on ASUS.
- Initial stationary menu pass timing moved from about 1.59 to 1.35 ms/frame
  for the light-accumulation target. User overlay report: GPU 92-94% before,
  89-91% with the candidate. The reverse control returned to about 1.59 ms/frame with the same seven
draw commands and 30 indices. Four complete five-second target reports per
mode are retained in `summary.json`; the repeat-off comparison supports the
local pass-time reduction. The optimization is left enabled on the ASUS;
GPU timestamps are then disabled for the final ordinary-run check.

These are one-device interactive observations, not isolated hardware occupancy
counters. GPU interval measurements can include queue bubbles and profiling
can perturb execution. The overall game queue interval remained around
14.4 ms at a 60 FPS cap, so no total GPU-time/FPS increase is inferred from
that unchanged interval. The user confirmed no visible menu artifacts in the final ordinary run.
The final log confirms timestamps=0, zero-source specialization=1 and about
59.94 renderer flips/s. The final repeated 89-91 reading was called FPS in
chat, although the earlier message explicitly identified that range as GPU
utilization and the log is capped at 60; it is not evidence of 89-91 FPS.
Full image comparison, long races and other GPUs remain untested. Raw artifacts live in ignored
`performance-results/2026-10-19/asus-zero-source/`.
