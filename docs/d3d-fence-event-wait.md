# Android D3D fence event wait

## Observed problem

The Zenfone 7 Pro video profile after installing native YUV conversion
(`performance-results/2026-10-20/asus-menu/video-native-profile.data`) shows:

- Native YUV conversion: 0.83% of application CPU cycles, compared with
  20.75% for the previous lifted function in the earlier video capture.
  These separate profiles do not establish a 25-fold conversion speedup.
- The main thread still uses about 4600 CPU ms per 5000 ms at 60 flips/s.
- 71.82% of **main-thread** cycles are under `sub_001D0340_gen`, the title's
  D3D_BlockOnTime. These are call-chain totals, not additive self costs.
- 41.91% of main cycles appear under `recomp_spin_wake`; most of those
  pass through `pthread_cond_broadcast` and futex work.

The remaining CPU activity is dominated by active semaphore polling and
repeated hardware-thread wakeups. Faster video conversion leaves more time
for this polling, so the utilization overlay need not fall.

## Change

The spin-hint pass now supplies the polling loop's guest PC and snapshots
of its local ax/cx/dx/si registers at its existing 64-iteration yield point.
The ordinary pause and hardware wake cadence is retained. Old generated
`RECOMP_SPIN_HINT()` sites still work without a native handler.

`src/d3d_fence_wait.c` recognizes only PC `0x001D0470`, the final semaphore
loop in this executable. Its registers contain:

- dx: the semaphore guest VA;
- si: submitted sequence;
- ax: submitted sequence minus requested fence.

The bridge first checks handler eligibility under guest ownership. For an
eligible poll it releases the guest token, waits, and reacquires ownership,
restoring the nesting depth. This works with the current priority scheduler
as well as the legacy GIL. DISPATCH contexts and unmatched/disabled polls
retain the original yield path. The executor/fiber path is unchanged.

The existing BACK_END_WRITE_SEMAPHORE_RELEASE write publishes the same value
at the same point and notifies waiting host threads. Publication and the
condition wait share a mutex, preventing a lost notification between the
completion check and parking. The unsigned completion predicate matches the
original loop, including sequence wrap. Android uses a monotonic deadline.
Each host wait is bounded to 1 ms so shutdown or an unnotified write returns
control to the original polling loop. A normal completion wakes immediately.

The lifted D3D function remains intact: kickoff, callbacks, alternate event
paths, register effects, return convention, and fence completion checks are
preserved. No fence value is forged and no frame-rate limit is added.

Default enabled on Android. `debug.nfsu.fence_wait=0` selects the original
poll/yield behavior. The property is checked every 128 matching yield points;
`[FENCEWAIT] native event wait=N` confirms transitions. Other platforms keep
their original policy.

## Validation

- `tests/nv2a_fence_wait.c`: original predicate, sequence wrap, completion
  before parking, concurrent notification, partial progress, multiple waiters,
  and bounded timeout. Host ASan/UBSan and Android ARM64 UBSan-trap passed.
- `tests/recomp_spin_pc.c`: guest PC and current lifted local registers reach
  the runtime; legacy spin hints retain their cadence. Host ASan/UBSan passed.
- `tests/d3d_fence_poll.c`: production handler exact-PC dispatch and live
  bypass. Android ARM64 UBSan-trap passed.
- Extended `tests/gil_handoff.c` and `tests/guest_scheduler_bridge.c` check
  that recognition holds ownership, actual waiting releases it, and nested
  ownership is restored. Existing contention tests remain in those harnesses.
- Spin-hint generator regression: 7 tests passed.

`assembleDebug lintDebug` passed. After the user's manual installation on
2026-10-20, the video run logged `[FENCEWAIT] native event wait=1` with the
priority guest scheduler enabled. Main-thread CPU time fell from about
4600 ms per 5000 ms to 733–864 ms per 5000 ms in the sampled stable windows.
Renderer windows recorded approximately 297–300 flips per 5 seconds. The
user reported aggregate overlay CPU falling from about 25% to 12–14%.
These are separate runs with changing movie content and uncontrolled DVFS;
they do not establish an exact energy reduction or screen presentation FPS.

The later 10-second `fence-on-profile.data` capture includes a transition
into the menu; do not label that entire sample as a pure video profile.
The user still reported GPU 99% in Options → Camera after this change.
Visual/audio, racing, background lifecycle and sustained thermal validation
remain incomplete. The fence change is a CPU improvement, not a confirmed
fix for high GPU utilization.
