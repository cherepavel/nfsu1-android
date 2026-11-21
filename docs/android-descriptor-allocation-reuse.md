# Android descriptor allocation reuse

12 November 2026. Feature branch `codex/reuse-descriptor-allocations`, based
on `develop` at `df44f78` (vertex cache and fused index uploads included).

## Problem and change

The ordinary Android Vulkan path allocated a fresh descriptor set, updated
three UBO and four combined-image bindings, and bound the set for every guest
draw. Presentation similarly allocated a separate one-image set. At the next
fence completion the whole descriptor pool was reset, discarding allocations.

Each frame slot now retains two ordered lists of allocated sets, one per
layout. A draw takes the next handle and **updates all its descriptors** before
the existing ordinary bind. New sets are allocated only when that layout's
high-water count grows. At fence completion only the list cursors reset.
There is no content hash, descriptor-content cache, dynamic UBO binding,
shader change or GPU buffer representation change. `vkUpdateDescriptorSets`
and `vkCmdBindDescriptorSets` still occur on every draw. This targets allocation
overhead, not the entire descriptor cost or Xbox command translation.

This differs from the earlier dynamic-UBO experiment in
[vulkan-dynamic-descriptors.md](vulkan-dynamic-descriptors.md): it keeps the
same ordinary descriptors and no dynamic offsets, including on Mali.

## Ownership and bounds

Every set is handed out at most once between resets. Frame slots have
independent ownership. The existing `android_frame_reset()` runs only after
the slot's final same-queue fence, or after the ring-full synchronous flush
wait. Early submissions do not reset the lists: sets referenced by an earlier
pending command buffer cannot be rewritten while recording the next one.
The existing command-pool reset still occurs before subsequent recording.

The former 8192-set limit per fence epoch, including presentation, remains.
Guest and presentation peaks can occur in different epochs, so up to two
8192-set pools may be retained per frame slot, covering a combined layout
high-water count of up to 16384. A second pool is created only when needed.
Normally one pool per slot is sufficient. CPU handle arrays grow on demand
and are bounded at 128 KiB per slot on ARM64. Driver allocation memory is
retained to the high-water count and is driver dependent; it is not measured
by this test. Pools and lists have the renderer/device's process lifetime,
matching the existing renderer, which has no device teardown path.

Retained sets may temporarily contain references to resources retired after
the fence. They are never used again without a full update. Sets do not
extend resource lifetimes. Layout mismatches, host allocation failures and
driver failures are rejected; the adapter retains its fatal error policy.

## Selection and counters

Reuse is enabled by default for Android's ordinary-descriptor path.
`debug.nfsu.descriptor_reuse=0` restores the previous allocation/reset path.
Exact `1`, empty and unset select reuse; invalid nonempty values select the
previous path. `RECOMP_VK_DESCRIPTOR_REUSE`, if present, takes precedence,
including an empty value. Fully restart the process after changing selection.

Explicit dynamic-descriptor and available native-push experiments take
precedence and keep their old layouts and behavior. If requested native push
is unavailable and the ordinary fallback is used, reuse can operate on it.
Startup logs `[VKSET] requested=1 active=1` when this path is selected.

Every 300 flips `[VKSET]` reports successful `requests`, `reused`, newly
`allocated` sets and `new_pools` since the preceding report. `retained_guest`,
`retained_present` and `frame_pools` describe only the slot being reset.
Counters are simple integers; there are no per-draw clocks or property reads.
After warmup, a stable workload should have almost all requests reused, with
allocations only when a slot/layout reaches a new peak.

## Validation

- Host ASan/UBSan test uses the production allocation helper and extracted
  production adapter/reset/selector, checking fresh contents on every bind,
  changing offsets/ranges/images/samplers, two-slot isolation, unique sets
  across multiple command buffers, both layouts, fence-complete reuse,
  layout-demand changes at the 8192 limit, driver failures and explicit
  rollback. Default/empty/invalid/property/environment cases pass.
- Existing dynamic-descriptor and native-push host regressions pass ASan/UBSan.
- NDK ARM64 build of the real-driver test passes `-Wall -Wextra -Werror`.
- `assembleDebug lintDebug` succeeds with register-block and executor-TLS
  arguments enabled. Existing compiler warnings are unchanged.
- Poco 24095PCADG, Mali-G615 MC2, driver 184553472: 4096 compute dispatches
  across two ordinary/reuse modes, two frame slots and eight epochs per mode.
  Three changing UBO offsets/ranges and four changing sampled-image bindings
  produce 16384 uint components matching CPU exactly. Each epoch has two
  submissions, with the first allowed to remain pending during recording of
  the second. Reuse mode allocates 512 sets initially and reuses them 1536
  times. No mismatch.
- The independent CPU benchmark records 2048 guest plus one presentation
  descriptor request per epoch, using the production reset/update/bind path,
  actual graphics-stage descriptor layouts, two frame slots, two warmup and
  32 measured epochs. Five pairs alternate mode order. Thread CPU time per
  request was 458.0–470.1 ns for legacy and 266.4–273.5 ns for reuse,
  **1.705–1.745x** faster for this isolated work. Reuse has zero allocations
  during measurement. Command-pool reset/begin/end, GPU work and rendering
  are outside the measured interval. Normal device clocks, no FPS claim.

Raw device results are saved locally under
`performance-results/2026-11-12/descriptor-reuse/`. Those results do not
establish an end-to-end game gain. The user installed the candidate APK and
reports that racing looks correct. This is visual gameplay feedback, without
a measured FPS/CPU comparison; transitions through menus, loading and returning
from a race were not separately reported.

## Reproduction

```sh
python3 tests/vulkan_descriptor_reuse.py --sanitize
python3 tests/vulkan_descriptor_reuse.py --gpu --compile-only \
  --cc "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang" \
  --output-dir /tmp/nfsu-descriptor-reuse-arm64
"$ANDROID_NDK/shader-tools/darwin-x86_64/glslc" \
  tests/vulkan_descriptor_reuse.comp -o /tmp/nfsu-descriptor-reuse.spv
adb push /tmp/nfsu-descriptor-reuse-arm64/descriptor-reuse-gpu \
  /tmp/nfsu-descriptor-reuse.spv /data/local/tmp/
adb shell /data/local/tmp/descriptor-reuse-gpu /data/local/tmp/nfsu-descriptor-reuse.spv
adb shell /data/local/tmp/descriptor-reuse-gpu --bench
```

These commands do not install/start/stop the game or change its properties.
