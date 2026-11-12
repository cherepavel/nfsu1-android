#!/usr/bin/env python3
"""Compare exact sample snapshots and legacy texture-cache decisions."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="cc")
parser.add_argument("--output-dir", type=Path, default=Path("/tmp/nfsu-texture-hash"))
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--compile-only", action="store_true")
parser.add_argument("--integration", action="store_true")
args = parser.parse_args()
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
source = (root / "xboxrecomp/src/nv2a_vk/nv2a_vk.c").read_text()
a = source.index("static uint32_t bytes_hash(")
b = source.index("static uint32_t tex_bytes(", a)
(out / "texture_hash_reference.h").write_text(source[a:b].replace("bytes_hash(", "legacy_hash("))
if args.integration:
    for name, start, end in [
        ("texture_type.h", "typedef struct {\n    uint32_t va, color, w, h, pitch, hash, bytes;", "/* RECOMP_VK_MIPS=0"),
        ("texture_globals.h", "#define VK_MAX_TEX 1024", "static int tex_size_from_format("),
        ("texture_bytes.h", "static uint32_t tex_bytes(", "static uint32_t swizzle_index("),
        ("texture_free.h", "static void tex_free(", "/* Copy the image's texels"),
        ("texture_get.h", "static VkImageView tex_get(", "/* A cube map whose faces")]:
        a = source.index(start); b = source.index(end, a)
        (out / name).write_text(source[a:b])
command = shlex.split(args.cc) + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
    "-fstack-protector-strong", "-I" + str(root / "xboxrecomp/src/nv2a_vk"), "-I" + str(out)]
if args.sanitize:
    command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
binary = out / ("vulkan-texture-samples" if args.integration else "vulkan-texture-hash")
if args.integration:
    command += ["-Wno-unused-variable", "-Wno-unused-function", "-I" + str(root / "third_party/SDL/src/video/khronos")]
name = "vulkan_texture_samples.c" if args.integration else "vulkan_texture_hash.c"
subprocess.run(command + [str(root / "tests" / name), "-o", str(binary)], check=True)
if not args.compile_only:
    subprocess.run([str(binary)], check=True)
    if args.integration:
        for value, prop, expected in [(None, None, 1), (None, "", 1), (None, "1", 1),
                (None, "0", 0), (None, "invalid", 0), ("", "0", 1), ("0", "1", 0),
                ("1", "0", 1), ("invalid", "1", 0)]:
            env = os.environ.copy()
            for key, setting in (("RECOMP_VK_TEXTURE_SAMPLES", value), ("TEXS_TEST_PROPERTY", prop)):
                env.pop(key, None)
                if setting is not None: env[key] = setting
            subprocess.run([str(binary), "--selector", str(expected)], env=env, check=True)
        print("Texture snapshot selector: default-on, rollback, environment precedence and caching: PASS")
