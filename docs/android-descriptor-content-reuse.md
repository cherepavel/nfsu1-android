# Android consecutive descriptor contents

14 November 2026. Feature branch `codex/descriptor-content-reuse` from
`develop` at `6a65066`, including accepted manual-wrapper O2.

## Measured problem and scope

The [fresh tablet profile](android-cpu-profile-2026-11-14.md) records the
renderer near 95% of one core. Its ordinary descriptor adapter accounts for
21.81% inclusive renderer samples. The previous allocation reuse avoids
reallocating sets, while still updating and binding all descriptors for each
request. The inclusive share contains other work and is not a savings estimate.

This feature recognizes consecutive requests whose ordinary descriptor
contents and command context are exactly the same. On a match the previous
immutable set remains bound; allocation, `vkUpdateDescriptorSets` and
`vkCmdBindDescriptorSets` are all skipped. A/B/A still issues all three
updates/binds. There is no hash table, dynamic offset, new Vulkan feature,
resource allocation, shader/layout change, draw merging or quality change.
Existing native-push and dynamic-descriptor paths retain their behavior.

## Equality and lifetime

The key contains 21 explicit 64-bit words for the seven guest bindings:
each UBO's buffer/offset/range and each image binding's sampler/view/layout.
Presentation has its own one-image/three-word shape. The key contains no
caller-array pointers or Vulkan structure padding. Ignored write members and
the overwritten `dstSet` do not participate. Noncanonical binding order,
arrays, unsupported types, missing payloads and nonempty `pNext` bypass reuse.

A hit additionally requires the same command-buffer handle, recording
generation, pipeline bind point, pipeline-layout handle, descriptor-set-layout
handle, and descriptor count. Set zero is the only accepted set index.
Both frame slots' fence-complete resets and ring-full synchronous resets
invalidate the saved state before pools/resources can be reused or destroyed.
All three production command-buffer begin paths increment the existing
recording generation; early submissions therefore cannot inherit a hit.

The active ordinary path has one binding entry point. It never updates the
already recorded previous set on a hit; misses retain the allocation helper's
at-most-once set assignment within a fence epoch. Retired resource handles
cannot become a valid saved key after reset. UBO data is still read through
the same buffers/ranges; no sample/uniform value is cached by this feature.

Vulkan descriptor bindings remain usable across compatible pipeline changes;
requiring identical pipeline-layout handles here is conservative. See
[pipeline layout compatibility](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html#descriptorsets-compatibility).
Render-pass boundaries and push constants do not replace this adapter's
descriptor binding. Future direct set binds on the ordinary path must
invalidate this saved state or participate in its tracking.

## Miss cost and selection

The first implementation added 2.7–5.5% CPU cost in the standalone
all-changing workload. After eight consecutive misses, optimized mode now
bypasses the next eight requests, then probes two consecutive requests per
ten until a hit restores full checking. Two probes are necessary to discover
consecutive repetitions. Bypassed requests retain every original API call;
their contents are not saved. A frame reset also resets this backoff.

`debug.nfsu.descriptor_contents` / `RECOMP_VK_DESCRIPTOR_CONTENTS`:

- Unset, empty or exact `1`: optimize, with miss backoff.
- Exact `2`: count every matching consecutive request, retaining all original
  update/bind calls and disabling backoff.
- `0` or any other nonempty value: previous update/bind behavior.

The environment takes precedence, including an empty value. Selection occurs
once at startup. The feature is active only when ordinary descriptor allocation
reuse is active. `descriptor_reuse=0`, native push or dynamic descriptors keep
it inactive. Startup reports `[VKCONT] requested=N active=M`.

Every 300 flips, `[VKCONT]` reports requests, matches, skipped update/bind
pairs, skipped individual writes, unsupported shapes and bypassed requests.
With optimization enabled, matches are a lower bound on possible matches
because backoff does not inspect some requests. Mode 2 measures all candidates.
The existing `[VKSET] requests` now counts the allocation-helper calls remaining
after these skips. No per-draw clocks or property queries are added.

## Correctness checks

Host ARM64 ASan/UBSan and native tablet tests pass through the extracted
production adapter/reset and the production allocation/content headers:

- Every one of the 21 guest binding fields, the three presentation fields,
  poisoned structure padding, changed caller pointers and ignored members.
- Verification of what the mock GPU still sees after skipped binds; A/B/A,
  pipeline-layout/bind-point/recording changes, frame-slot isolation, reset,
  opaque extension bypass, backoff and rediscovery, count-only and rollback.
- More than 8192 identical requests without consuming the set budget; original
  allocation-limit/failure/ownership tests remain passing.
- Twelve separate-process selector cases, including exact values, property
  fallback and environment precedence.

On the user-authorized tablet 24076RP19G / Android 16, Mali-G52 MC2 driver
205524992, the real-driver compute oracle passes **6144 dispatches / 24,576
uint components**, zero mismatches against CPU. It uses ordinary, optimized
and count-only modes, three UBOs and four sampled images, two frame slots,
eight epochs per mode and two submissions per epoch. Repeated contents cross
the submission boundary; the first submission can remain pending while the
second is recorded. The oracle checks 3484 matches across optimized/count-only
modes and 1742 skipped update/bind pairs in optimized mode.

The original ordinary/allocation-reuse GPU regression also passes 4096
dispatches / 16,384 components, zero mismatches. Dynamic-descriptor and
native-push host regressions pass ASan/UBSan. These are binding/resource tests;
they do not prove whole-game image equivalence or frame pacing.

## Isolated CPU measurements

Only the standalone process is pinned to CPU7. Clocks, game properties and
game scheduling are unchanged. Each mode records 2048 guest plus one
presentation request per epoch, two frame slots, two warmup and 32 measured
epochs. Five pairs alternate mode order. Both baseline and candidate use
allocation reuse; only descriptor-content optimization changes.

Timing includes descriptor-write preparation, frame descriptor reset and
ordinary update/bind work. It excludes command-pool reset/begin/end, submits,
draw execution and GPU work. The final results are:

| Consecutive group | Baseline ns/request | Candidate ns/request | Result |
| --- | ---: | ---: | --- |
| Every request changes | 1701.9–1740.2 | 1742.4–1777.3 | 0.44–4.01% more paired CPU time |
| Four identical requests | 1715.9–1748.2 | 535.4–544.5 | 3.20–3.24x throughput |
| Sixteen identical requests | 1721.9–1777.4 | 209.1–211.7 | 8.23–8.40x throughput |

The no-hit case retains a small cost. The benefit depends on actual game
repetition; these synthetic repeat patterns do not establish its hit rate,
FPS, total renderer CPU, temperature or power improvement. Live counters and
a gameplay check remain pending for the candidate APK.

## Reproduction and APK

```sh
python3 tests/vulkan_descriptor_contents.py --sanitize
python3 tests/vulkan_descriptor_reuse.py --sanitize
python3 tests/vulkan_descriptor_contents.py --gpu --compile-only \
  --cc "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang" \
  --output-dir /tmp/nfsu-descriptor-contents-arm64
"$ANDROID_NDK/shader-tools/darwin-x86_64/glslc" \
  tests/vulkan_descriptor_reuse.comp -o /tmp/descriptor-contents.spv
adb push /tmp/nfsu-descriptor-contents-arm64/descriptor-contents-gpu \
  /tmp/descriptor-contents.spv /data/local/tmp/
adb shell /data/local/tmp/descriptor-contents-gpu /data/local/tmp/descriptor-contents.spv
adb shell taskset 80 /data/local/tmp/descriptor-contents-gpu --bench
```

`assembleDebug lintDebug` passes with register-block/TLS-argument generation,
zero errors and 76 lint warnings. APK signature and 16 KiB zip alignment pass;
packaged/unstripped native build IDs match. Exact source hashes, matching
libraries, frozen APK and raw checks are retained under ignored
`performance-results/2026-11-14/descriptor-contents/`.

- APK: `android/app/build/outputs/apk/debug/app-debug.apk`.
- SHA-256: `b1d06a83531a91a3a4df88dbc5aa820f695994790bf2d98d9cfa3907e3fd6435`.
- Native build ID: `b1fde48a6f95867fd8a3ad97a5d84d13c03fd40b`.

The user installs/starts the candidate and checks racing, HUD, menus, loading
and sound before merge. No APK installation, restart, game input or persistent
property change has been performed by the agent. Prior feedback for the
manual-wrapper APK does not validate this new APK.
