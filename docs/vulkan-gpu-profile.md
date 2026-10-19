# Opt-in Vulkan GPU queue timing

Set `debug.nfsu.gpu_profile=1` on Android, then restart the game process.
Other platforms accept `RECOMP_VK_GPU_PROFILE=1`. Disabled by default.
The selected graphics queue must support timestamps. `[GPU]` reports support
and `[GPU5]` reports five-second aggregates of completed frames.

- `game_ms`: GPU queue interval from frame start to completed guest rendering,
  including uploads, mip generation, reflection assembly and barriers.
- `present_ms`: subsequent interval through final FXAA/blit and layout transition.
- `game_max_ms`, `present_max_ms`: largest individual sampled intervals.
- `passes`: rendering instances, including the final FXAA pass.
- `full_barriers`: calls to the renderer's global ALL_COMMANDS memory barrier;
  excludes other targeted image/buffer barriers.
- `blits`: image blit commands, including reflection and mip generation.

Three timestamps per frame are read only after its existing final fence has
completed, without a query WAIT bit or extra submissions/waits. Query pools
are reset on reuse; early submits and ring flushes retain the same pool.
Timestamp wrap uses the selected queue's valid-bit count. An unavailable
result is skipped. Unsupported/failed query pools leave rendering unchanged.

These are queue elapsed intervals, **not GPU busy percentages or isolated
shader execution times**. They can include idle gaps between CPU submissions,
ring flushes and semaphore waits. The final interval includes presentation
image readiness waits where they affect execution. Neither interval measures
scanout or the host duration of vkQueuePresentKHR. Compare warmed, identical
scenes and settings; verify with profiling disabled before attributing a
performance difference to a renderer change.

Restore the default with `adb shell setprop debug.nfsu.gpu_profile 0` and a
process restart. No render policy or graphics settings are changed by this
instrumentation.

`[GPUPASS]` lists the five guest render targets with the largest aggregate
pass interval per completed frame, with stored dimensions and pass count.
The first 64 guest passes in each frame have paired bottom-of-pipe timestamps
outside rendering instances; later passes still contribute to `game_ms`.
The aggregation holds 64 target/dimension combinations; extra combinations
are reported as overflow time. Final FXAA is covered by `present_ms` rather
than the guest target table. Pass intervals exclude the global barrier before
the pass and work recorded between passes. Timestamp commands can perturb
execution, so do not equate these sums with an uninstrumented frame time.

## ASUS Adreno 650 observation, 2026-10-19

The O2 debug APK built and was installed on ASUS ZS671KS. Timestamps are
supported (48 valid bits, 52.0833 ns period). In the user's stationary car menu,
250% scale, FXAA/16x/square pixels on, reflections on: about 17.77 ms guest
queue interval plus 2.22 ms final output, 22 rendering instances, 24 global
barriers and 14 blits per frame. With reflections off and the other listed
settings restored, the interval was about 16.44 + 2.24 ms, 15 instances/barriers
and zero blits. This excludes reflections as the main cost in this scene.

With per-pass timing enabled and reflections off, two alternating 2133x1200
targets (83240000, 83370000) together accounted for about 9.1 ms/frame; target
82A4ED80, also 2133x1200, about 3.16 ms/frame. The final output was about
2.18 ms. These guest addresses do not identify semantic passes. The data
points to large-target rendering for further investigation; it does not
establish a safe barrier-removal, shader, or attachment-load optimization.

At 100%, FXAA off, anisotropy 1x and square pixels off (reflections still on),
the earlier coarse measurement showed about 13.4 + 0.3 ms and 60 renderer
flips/s. Multiple settings changed together, so this does not isolate scale.
All observations are one interactive session with changing scenes/settings
and GPU power state, not a controlled benchmark or a measured optimization.
Raw logs and summaries are local ignored files in
`performance-results/2026-10-19/asus-menu/`.

Guest pass reports also include `draws_per_frame` (actual indexed Vulkan draw
commands) and `indices_per_frame` (index count multiplied by instance count).
These count recorded work rather than raw guest draw requests; pending
instancing is counted when flushed. Only the first 64 guest passes are covered,
matching the per-pass timestamp limit.
