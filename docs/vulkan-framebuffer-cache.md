# Android framebuffer reuse

The Vulkan 1.1 adapter previously created a VkFramebuffer at every render-pass
begin and destroyed it when that frame's fence completed. In the ASUS Camera
menu this meant roughly 17 rendering passes including final output per frame, or over
1,000 framebuffer creations/destructions per second at 60 flips/s. A CPU-cycle
sample on 2026-10-20 identified vkCreateFramebuffer and allocator work in the
renderer. This observation does not establish that framebuffer allocation
explains the GPU-utilization overlay or all heating.

The adapter now keeps up to 128 immutable framebuffers, keyed by render pass,
attachment count, color/depth image-view handles and stored dimensions. The
adapter always uses one layer. Reuse changes no draw, attachment load/store,
image layout, render scale, FXAA or frame-rate setting.

Eviction defers framebuffer destruction to the recording frame's existing
fence. Guest-image retirement removes every cached framebuffer referencing
that view and retires them with the image, before its view is destroyed.
Swapchain replacement removes its cached framebuffers only after the existing
device-idle wait. Ordinary frame reuse resets descriptor pools and frees
retired framebuffers, while cache entries remain available across frames.
All operations run on the renderer thread.

## Same-process comparison

Default: enabled. Android supports a live bypass:

```sh
adb shell setprop debug.nfsu.framebuffer_cache 0
adb shell setprop debug.nfsu.framebuffer_cache 1
```

The property is checked once per 300 flips, without restarting the game.
`[VKFB] cache=N next_cache=M hits=... creates=...` reports the previous
window's mode and counts, then applies M to subsequent render-pass begins.
Exclude windows spanning a mode switch. The bypass creates and defers each
framebuffer exactly as before; retained cached entries can be reused when
the cache is enabled again. Image retirement invalidates entries in both modes.

For an A/B/A CPU comparison, keep the same APK and Camera menu, allow each mode
to settle, and compare complete PERF5 renderer windows normalized by flips.
GPU queue timestamps can contain CPU submission gaps and cannot be read as
GPU busy percentage. A CPU reduction alone does not establish lower GPU
occupancy or temperature.

## Lifetime regression

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Ithird_party/SDL/src/video/khronos tests/vulkan_framebuffer_cache.c \
  -o /tmp/nfsu-framebuffer-cache-test
/tmp/nfsu-framebuffer-cache-test
```

The regression uses the production cache with a mock Vulkan driver. It covers
cross-frame reuse, key changes, independent color/depth retirement, recycled
image-view handles, live bypass, creation failure, bounded LRU eviction and
deferred destruction. Host ASan/UBSan and ASUS ARM64 UBSan-trap runs passed.
These checks do not replace actual-driver rendering and lifecycle validation.

## ASUS menu validation, 2026-10-20

`assembleDebug lintDebug -Pnfsu.genOpt=-O2` passed. The user signed and
installed the ordinary `org.nfsu.android` APK; a separate application package
was not installed. The existing original signing key is not available here.
The installed native build ID is `b678b6e8168dfaffceef8e59c44f02bc1f7f042d`,
matching the local `-O1` build used for final CPU symbolization. Both sides
of the live comparison use that same installed binary.

Same-process Camera-menu comparison used `0 -> 1 -> 0 -> 1`, with 250% scale,
reflections and FXAA enabled, saved anisotropy 16, square pixels enabled,
60 FPS and Blur/Bloom Off. GPU profiling, alpha specialization and zero-source
specialization were disabled. Cache reports changed from 5,100 creates per
300 flips to 5,100 hits and zero creates after warmup.

Fifteen-second simpleperf hardware-counter windows for the renderer thread:

| Mode | Instructions | CPU cycles | Thread CPU time | Mean CPU frequency |
| --- | ---: | ---: | ---: | ---: |
| On, first | 14.228 billion | 8.467 billion | 6.300 s | 1.342 GHz |
| Off, reverse | 17.822 billion | 11.057 billion | 10.452 s | 1.057 GHz |
| On, repeat | 13.760 billion | 7.760 billion | 4.705 s | 1.649 GHz |

The enabled windows execute about 20-23% fewer renderer instructions per
15 seconds. Renderer flip rates were approximately 58-60/s. The animated
car/camera and CPU frequency were not locked, so CPU-time differences cannot
be attributed entirely to the cache. This is a CPU-side improvement; the
user reported GPU utilization of 81% before and 88% with the cache, and no
reduction in GPU utilization is claimed. Neither GPU energy nor sustained
thermal improvement has been established.

A short Perfetto trace exposed CPU frequency changes but no GPU-frequency
samples. KGSL frequency/busy sysfs nodes denied shell access. GPU percentage
alone therefore cannot establish a fraction of the GPU's maximum throughput.
Raw APK backups, hardware counters, logs and the summary script are local
ignored artifacts in `performance-results/2026-10-20/asus-menu/`.
The final device state is `framebuffer_cache=1`, `gpu_profile=0`,
`alpha_specialize=0`, `zero_source=0`. The menu continued rendering with the
same game-process PID. Actual-driver long races, rescaling and background
surface recreation have not been validated in this session.
