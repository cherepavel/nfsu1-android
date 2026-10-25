# GPU conversion / native-path audit

2026-10-20, while investigating high menu GPU utilization on Zenfone 7 Pro.
This audit identifies code paths; it does not establish a new GPU optimization.

## Verified paths

- `nv2a_vk.c:compile_thread` translates generated GLSL into SPIR-V 1.3 for
  Vulkan 1.1. `compile` reuses source-hashed SPIR-V, and `pipe_get` supplies
  shader modules to Vulkan pipelines. The renderer has no per-fragment Xbox
  instruction interpreter running on the GPU; shader execution uses the
  driver's compiled pipeline.
- Movie `color == 0x12` texture uploads already contain BGRA bytes matching
  `VK_FORMAT_B8G8R8A8_UNORM`. `tex_fill` uploads these directly. The separate
  YUV conversion is the CPU function already replaced with NEON.
- Texture format decode / unswizzle occurs on the CPU when guest bytes change.
  `tex_get` hashes and caches those textures; unchanged textures bypass fill.
  This is distinct from an operation executed for every shaded fragment.
- Reflection assembly and mip generation use Vulkan image blits. The final
  output is a native full-screen FXAA shader. These can consume GPU time but
  are not an x86/MMX emulation path.

## Cached programs inspected

Pulled the device's 128,700-byte `progcache.bin`: 55 valid records (2,340 bytes
each), 50 distinct fragment keys and 5 distinct vertex instruction streams.
Generated the fragment sources with the production `gl_psh.c` generator.

- Fragment combiner stage counts: 22 one-stage, 23 two-stage, 3 three-stage,
  and 2 five-stage variants.
- Source texture fetch counts: 22 one-fetch, 17 two-fetch, 2 three-fetch,
  and 9 four-fetch variants. These are generated source counts, not native
  machine instruction counts or evidence that all samples survive optimization.
- Vertex stream lengths: 12, 24, 26, 48 and 74 Xbox instructions.
- No cached vertex program writes constant registers. Thus the generator's
  expensive-looking `cl[192]` per-vertex copy path is not used by these records.

The cache is a history of compiled programs, not a per-draw profile of the
currently visible menu. The record counts do not identify the hottest shader.
Raw cache, generated sources and `shader-inspection.json` are local ignored
artifacts in `performance-results/2026-10-20/asus-menu/`.

## 2026-10-21 command-management follow-up

The compact shader and barrier/depth trials did not establish a useful GPU
improvement. A new [command/resource-management audit](android-command-path-audit.md)
uses a clean CPU profile and identifies a supported native push-descriptor
API that the Android compatibility branch bypassed. The prepared opt-in path
targets that measured CPU/driver hotspot; no GPU/thermal saving is claimed.

## Earlier shader candidate

Some generated one-stage fragment formulas can be represented much more
compactly than the generic combiner source. However, removing source-level
temporary variables and zero expressions may produce the same driver binary
after dead-code elimination. Before maintaining replacement shaders, correlate
the actual menu draws with their program keys and measure the candidate.
Do not claim that source length explains 89–90% GPU utilization, or promise
a 40% overlay result from rewriting a shader.

The subsequent compact-shader numeric test passed, but the live improvement
was only about 0.3%; it remains off. The native-push and per-frame dynamic
descriptor trials also failed to establish a useful renderer win; their
complete return/repeat measurements are in
[the command audit](android-command-path-audit.md) and
[dynamic descriptor report](vulkan-dynamic-descriptors.md).

The current hypothesis concerns Android presentation: the actual game layer
uses CLIENT composition with ROT_90, including without Game Genie. The
[presentation audit](vulkan-presentation-prerotation.md) records that evidence
and an opt-in correction which folds pixel rotation into the existing final
pass. Installed return/repeat testing subsequently confirms CLIENT/ROT_90 →
DEVICE/transform 0 and zero client compositions, with and without Game Genie,
at approximately 60 FPS. Picture, resume and both landscape orientations pass
on this phone. The final source enables that path by default. This removes one
confirmed GPU cost; the remaining high load/thermal issue is not declared solved.
