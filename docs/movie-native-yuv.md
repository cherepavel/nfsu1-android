# Native movie YUV conversion

`sub_0019D840` converts pairs of Y samples plus one U/V sample to eight
bytes of 32-bit movie pixels. The lifted MMX implementation accounted for
20.75% of application CPU-cycle samples in the ASUS Zenfone 7 Pro video
capture (`performance-results/2026-10-20/asus-menu/video-profile.txt`). This
is a share of sampled application cycles, not a device CPU-load percentage.

The manual wrapper calls `src/movie_yuv.c`, compiled at `-O2` even in Debug.
On AArch64 it uses NEON loads, wrapping 16-bit additions, and signed-word
to unsigned-byte saturation. It reads the original guest Y/U/V tables at
`0x25CC30`, `0x25D430`, and `0x25DC30`; coefficients, channel order, alpha,
and clipping are unchanged. It preserves the generated function's final
general/MMX registers, cdecl return, and four stack-save writes.

Unsupported spans, empty/non-pair output sizes, and overlapping input,
output, table, or stack spans fall back to `sub_0019D840_gen`. Other CPU
architectures use the generated implementation. The decoder itself and
the Vulkan movie upload/rendering path are unchanged.

On Android, `debug.nfsu.native_yuv=0` selects the generated version;
unset or `1` selects native. The property is polled every 4096 calls, and
`[MOVIEYUV] native NEON=N` logs changes. This allows comparison in the same
process without reinstalling. Leave deferred clears disabled during this
CPU comparison so the two experiments remain independently measurable.

## Correctness

`tests/movie_yuv_native.py` extracts the actual generated function (including
the `_gen` name after regeneration) as the reference for the C harness.
It compares every output byte, all general/MMX registers, the stack-save
writes, output guards, and unchanged input/tables. Full-width random table
words exercise 16-bit wrapping and both saturation boundaries.

```sh
python3 tests/movie_yuv_native.py --sanitize --output /tmp/nfsu-yuv-test
/tmp/nfsu-yuv-test
python3 tests/movie_yuv_native.py --cc /path/to/aarch64-linux-android30-clang --output /tmp/nfsu-yuv-test-arm64
adb push /tmp/nfsu-yuv-test-arm64 /data/local/tmp/nfsu-yuv-test
adb shell /data/local/tmp/nfsu-yuv-test --benchmark
```

Host ASan/UBSan and the Android ARM64 executable both passed all 16,777,216
Y/U/V triples, 1024 randomized rows (1–1024 pairs, unaligned outputs), and
fallback checks on 2026-10-20. The reference is extracted, not a separately
rewritten color formula.

## Isolated performance

On the attached Zenfone 7 Pro, 100,000 conversions of a 640-pixel row:

| Implementation | CPU time |
| --- | ---: |
| Lifted reference, first run | 647.525 ms |
| Native NEON | 142.874 ms |
| Lifted reference, repeat | 648.504 ms |

Both test paths were compiled with `-O2`. This is about 4.5 times faster for
the isolated conversion. It does not establish an application-wide CPU
reduction or resolve the separate high GPU utilization.

The user installed the APK at 22:45 on 2026-10-20. `[MOVIEYUV] native NEON=1`
and the new profile confirm execution of `nfsu_movie_yuv_native` (0.83% of
sampled application cycles). Main-thread CPU time remained about 4600 ms per
5000 ms. Call-chain analysis attributes 71.82% of main-thread cycles to
D3D_BlockOnTime's polling path; see `d3d-fence-event-wait.md`. These profiles
do not establish the conversion's application-wide energy saving.
