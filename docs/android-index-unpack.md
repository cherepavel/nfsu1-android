# ARM64 index unpacking

`nv2a_pb_exec_run` handles ARRAY_ELEMENT16 packets whose words contain two
16-bit indices, low half first. The renderer stores indices as uint32_t because
DRAW_ARRAYS also accepts 24-bit starts. A memcpy into that array is incorrect.

`nv2a_indices.h` widens eight indices per iteration using ARM64 NEON, with a
scalar tail and a scalar fallback on other targets. It preserves whole-pair
capacity limits, packet consumption, final register shadow, and primitive-off
behavior. Command source memory and the renderer-owned destination are disjoint.
Logging and draw ordering are unchanged.

Validation, 26 October 2026:

- 4,547,796 differential cases covering capacities 0..261, every valid initial
  count and packet lengths 0..131. Entire buffers including sentinels compared.
- macOS ARM64: Clang O2, ASan and UBSan fail-fast, PASS.
- Poco Android ARM64: NDK 30 Clang O2, PASS.
- `./gradlew assembleDebug`: PASS.
- Isolated on-device CPU-time experiment: 200,000 packets of 2,048 words,
  three alternating old/new pairs. Old 414.398/415.534/414.879 ms;
  NEON 103.117/103.184/103.192 ms, equal checksums. This uses separate noinline
  functions and a global index count matching the previous loop shape; it is
  not an end-to-end frame benchmark and does not establish an FPS improvement.

Test command:

```sh
cc -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-sanitize-recover=all tests/nv2a_indices.c -o /tmp/nfsu-indices-test
/tmp/nfsu-indices-test
```
