# Menu option observations, ASUS Zenfone 7 Pro, 2026-10-20

The user reported lower FPS with reflections Off, approximately -10 FPS with
Anti-Aliasing On and -5 FPS with Square Pixels On, all in Options → Camera.
These are useful reports, but the inverse reflection behavior was not reproduced
in the subsequent stabilized measurements. No rendering change was made here.

Source inspection confirms:

- Anti-Aliasing selects the port's full-screen FXAA shader, otherwise a linear
  present blit. It is separate from the guest surface's native AA dimensions.
- Square Pixels multiplies stored width by 4/3 for surfaces with aa_sx <= 1,
  when widescreen/square policy is active. At 250%, the main 640×480 target
  therefore changes from 1600×1200 to 2133×1200. It is not just an aspect flag
  at presentation. Changing this option reallocates/rescales existing targets
  and drops depth targets, so transition windows must be excluded.
- Reflections Off disables sampling the real cube and suppresses six guest
  environment views around the world-render call. With reflections On,
  cube assembly also ends the active render pass and inserts dependencies.
  A changed synchronization pattern is a possible explanation for an inverse
  performance result in principle, but is not demonstrated by these samples.

One process (PID 32348, renderer 32488), compact shader Off, alpha/zero-source
specializations On, GPU timestamps On, early submits 0, deferred clears Off.
The user operated the in-game settings. Most initial option changes were only
1–6 seconds apart; those mixed GPU5 windows cannot be used as steady comparisons.

With the same 250%/FXAA On/16x/Square On settings, the user then held reflections
Off (23:43:43.592) and On again (23:44:44.852). Stable Off windows have 10 guest
passes, 10 full barriers and 0 cube blits versus On's 17, 19 and 14. Queue game
interval is about 11.13 ms Off versus 12.35 ms On; final output stays about
2.24–2.25 ms. Renderer flips stay approximately 59.94/s in both modes.

Actual-present timestamps from SurfaceFlinger's game BLAST layer also show
approximately 60/s in the short stable Off and returning On samples (18 and 52
valid timestamps respectively, max gaps 16.83/16.77 ms). An earlier On sample
had one 33.1 ms gap among only 24 timestamps, illustrating that short samples
can include a missed refresh. The vendor exposes short, partially populated
latency buffers; these snapshots cannot establish long-term pacing, eliminate
all transient stutter, or rule out a different future thermal state. App flips
alone do not establish actual screen presentation rate.

Existing stable FXAA Off + Square Off windows show final output about 0.25 ms;
FXAA On + Square Off about 2.59 ms. The game interval also varies by mode, so
compare the whole queue interval rather than treating that difference as a
precise isolated shader benchmark. The observation is consistent with material
FXAA cost, while the logged renderer remains around 60 flips/s. It does not
reproduce a sustained 10 FPS drop in this run.

Raw logs and latency samples are ignored local artifacts under
`performance-results/2026-10-20/asus-menu/options-*`.
