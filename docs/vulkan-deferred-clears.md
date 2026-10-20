# Experimental deferred full clears

The 2026-10-20 ASUS video trace contains 13 guest rendering instances:
one main-target clear, four half-resolution quads, six clear-only reflection
targets, one light-accumulation fade and the final two main-target draws.
Final FXAA adds another instance. These extra operations are recorded during
video playback; that does not establish that every operation can be removed.

Android `debug.nfsu.lazy_clear=1` enables an experimental content-preserving
optimization. Default: 0. The property is polled every 300 flips, allowing a
same-process comparison. `[VKCLR]` confirms transitions. Other platforms keep
the original policy.

Full color clears, optionally accompanied by full depth AND stencil clears
of the same stored size, become pending attachment contents. A draw consumes
them with attachment LOAD_OP_CLEAR. Consecutive complete clears replace the
pending value without executing intermediate work. Partial rectangles and
individual depth/stencil aspects retain the original vkCmdClearAttachments
path. Clears within the active matching pass also retain that path.

Pending color contents are materialized before sampling, cube-map assembly,
rescaling, readback and presentation. Shared depth state belongs to the depth
image: clearing through another color target forces the active pass to
restart before its next draw. Smaller attachment extents materialize the
complete pending contents first. Image retirement may discard unobserved
pending contents, just as an immediately executed clear would disappear with
that image.

The Vulkan 1.1 render-pass adapter independently keys presentation layout and
attachment clear operations. Guest clears remain in GENERAL layout; final
FXAA retains COLOR_ATTACHMENT_OPTIMAL. Depth/stencil clear values and
clearValueCount follow the attachment load operations. Compatible pipeline
and framebuffer lifetime behavior is retained.

The production pass/clear helpers are exercised by
`tests/vulkan_deferred_clear.c`, including shared depth, repeated full clears,
partial-clear fallback, read materialization, unequal dimensions and live
property polling. Host ASan/UBSan and ASUS ARM64 UBSan-trap runs passed.
In the installed Zenfone build, a same-process video comparison on 2026-10-20
reduced average rendering instances from about 14.5 to 7.3–7.8, and full
barriers from 15.0 to about 8.0. A pass-sequence snapshot changed from 13 guest
instances to 7. Game GPU timestamp intervals remained about 6.0–6.3 ms, with
scene and clock variation uncontrolled. This does not establish a meaningful
GPU-utilization or thermal improvement. The property was restored to 0.
Actual-driver visual and lifecycle validation remains incomplete.
After the separate D3D fence CPU fix, the user still reported 99% GPU in
Options → Camera. No GPU-utilization fix is claimed by this checkpoint.

```sh
cc -std=c11 -Wall -Wextra -Werror -Wno-missing-field-initializers \
  -fsanitize=address,undefined -Ithird_party/SDL/src/video/khronos \
  tests/vulkan_deferred_clear.c -o /tmp/nfsu-deferred-clear-test
/tmp/nfsu-deferred-clear-test
```
