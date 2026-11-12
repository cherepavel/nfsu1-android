# Android RC build configuration

Build from the repository root with the established APK/signing task:

```sh
./build_debug.sh
```

After a successful build, the distributable RC artifact is
`android/app/build/outputs/apk/debug/app-debug.apk`. This remains the existing
Debug task; no new signing identity, package name, version code or Release-task
artifact is introduced. The two explicit Gradle flags select the tested
register-block and executor-TLS-argument code generation. Generated code retains
its existing -O2 optimization. The launcher records the source commit.

## Packaged defaults

These defaults are implemented in Android startup and Vulkan selectors, rather
than requiring users to reproduce the investigation's device properties.

| Mode | RC default | Override |
| --- | --- | --- |
| Guest runtime executor | On | debug.nfsu.executor=0 restores the previous runtime path |
| Dynamic-state command cache | On | debug.nfsu.state_cache=0 |
| Direct vertex upload cache | On on Android | debug.nfsu.vertex_cache=0; 2 measures reuse without skipping uploads; next process |
| Fused index upload | On on Android | debug.nfsu.index_fuse=0 restores the previous path; next process |
| Descriptor allocation reuse | On on Android ordinary-descriptor path | debug.nfsu.descriptor_reuse=0 restores per-draw allocation; next process |
| Early reflection assembly | On on Android | debug.nfsu.cube_early=0; environment override still takes precedence |
| Scoped render-pass consumer barrier | On on Android | debug.nfsu.pass_barrier=0, live at the next poll |
| Early-submit draw threshold | 300, unchanged | debug.nfsu.vk_kick=N, next process |
| Output-resolution UI experiment | Off | debug.nfsu.ui_native=1, experimental |
| Adaptive widescreen and FOV | On; native geometry at 4:3 | debug.nfsu.adaptive_wide=0; RECOMP_ADAPTIVE_WIDESCREEN takes precedence |
| Deferred full clears | Off | existing debug.nfsu.lazy_clear experiment |
| GPU / cubemap timestamps | Off | existing gpu_profile / cube_profile opt-ins |
| CPU performance counters | Off in ordinary play | debug.nfsu.perf=1, next process |
| Detailed diagnostics | Off | debug.nfsu.diagnostics=1, next process |

Detailed diagnostics enables CPU counters when perf is unset/empty; explicit
perf=0 still disables them. Startup executor and profiling overrides accept
exact 1; explicit 0 and invalid nonempty values select off. Clearing these
properties restores their packaged defaults. State-cache, cube and barrier
selectors retain their explicit rollback paths. Non-Android cube scheduling
and barrier defaults remain unchanged. No graphics option or quality setting
is lowered by this RC.

The investigation used kick=0 on Poco, while the general RC retains 300.
Thus it is not the exact same performance configuration: queue-time results
must not be advertised as a universal FPS/power gain for this RC. Menu scope
comparisons and initial race/image evidence are in [the barrier report](vulkan-pass-barrier.md).

## Local validation and device handoff

Host ASan/UBSan checks cover Android startup option defaults/rollback, early
cube selection and scheduling on host/simulated Android, and exact barrier
producer/consumer scopes, visibility, polling, profiling-off counters and live
rollback on both platform policies. The existing cubemap profiling helper
tests also pass. Android compiler syntax checks pass for android_host.c and
nv2a_vk.c with the configured register-block/TLS build flags; three existing
unused-symbol warnings remain in Vulkan code. No full Gradle build/install or
manual device restart is performed by the agent.
The CPU performance aggregation/disabled-mode regression passes ASan/UBSan
with its logging-only Linux thread-ID syscall stubbed on the macOS host.

The user builds/installs and starts this APK. Before treating a test device as
a clean default run, account for existing debug.nfsu properties and persistent
`nfsu1x_env.txt`: they survive APK replacement and can override the packaged
configuration, including enabling TRACE. Preserve the user's file rather
than deleting saves/settings to obtain a clean test.

In a run without overrides, startup should report executor=1, diagnostics=0,
performance counters=0, GPU mode=0/pass_timestamps=0/cube_timestamps=0,
early-submit threshold 300, native UI=0 and deferred clears=0. After rendering
starts, early assembly=1, state cache=1 and scoped_pass=1 confirm the selectors.
The new packaged defaults have host/syntax coverage; the user's new APK still
needs its first actual build/startup check with these defaults and kick=300.
Earlier device captures used the explicitly recorded older defaults/overrides.
