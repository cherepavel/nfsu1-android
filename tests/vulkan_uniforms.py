#!/usr/bin/env python3
"""Build uniform byte-equivalence, lifetime and CPU-work checks."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="cc")
parser.add_argument("--output-dir", type=Path, default=Path("/tmp/nfsu-uniforms"))
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--compile-only", action="store_true")
parser.add_argument("--gpu", action="store_true")
parser.add_argument("--shader", type=Path)
args = parser.parse_args()
out = args.output_dir.resolve(); out.mkdir(parents=True, exist_ok=True)
source = (root / "xboxrecomp/src/nv2a_vk/nv2a_vk.c").read_text()
for name, start, end in [
    ("uniform_types.h", "typedef struct {\n    float surf[4];", '#include "nv2a_vk_uniforms.h"'),
    ("uniform_colors.h", "static float byte_f(", "static VkCompareOp cmp_op("),
    ("uniform_zmax.h", "static uint32_t zmax_of(", "/* NV097 primitive")]:
    a = source.index(start); b = source.index(end, a)
    (out / name).write_text(source[a:b])
binary = out / ("vulkan-uniforms-gpu" if args.gpu else "vulkan-uniforms")
cmd = shlex.split(args.cc) + ["-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
    "-fstack-protector-strong", "-I" + str(root / "third_party/SDL/src/video/khronos"),
    "-I" + str(root / "xboxrecomp/src/nv2a_vk"), "-I" + str(out)]
if args.sanitize:
    cmd += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
name = "vulkan_uniforms_gpu.c" if args.gpu else "vulkan_uniforms.c"
subprocess.run(cmd + [str(root / "tests" / name), "-lm"] + (["-lvulkan"] if args.gpu else []) + ["-o", str(binary)], check=True)
if not args.compile_only:
    if args.gpu and not args.shader:
        parser.error("--gpu needs --shader or --compile-only")
    subprocess.run([str(binary)] + ([str(args.shader)] if args.gpu else []), check=True)
    if args.gpu:
        raise SystemExit(0)
    for environment, prop, expected in [(None, None, 1), (None, "", 1), (None, "0", 0),
            (None, "1", 1), (None, "invalid", 0), (None, "01", 0), ("", "0", 1),
            ("0", "1", 0), ("1", "0", 1), ("invalid", "1", 0)]:
        env = os.environ.copy()
        for key, value in (("RECOMP_VK_UNIFORM_CACHE", environment), ("UBO_TEST_PROPERTY", prop)):
            env.pop(key, None)
            if value is not None: env[key] = value
        subprocess.run([str(binary), "--selector", str(expected)], env=env, check=True)
    print("Uniform selection: default-on, rollback, exact values, environment precedence and caching: PASS")
