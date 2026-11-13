#!/usr/bin/env python3
"""Check byte-exact indexed texture tiles, production dispatch and CPU cost."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="cc")
parser.add_argument("--output-dir", type=Path, default=Path("/tmp/nfsu-indexed"))
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--compile-only", action="store_true")
parser.add_argument("--scalar", action="store_true")
parser.add_argument("--non-android", action="store_true")
args = parser.parse_args()
out = args.output_dir.resolve(); out.mkdir(parents=True, exist_ok=True)
source = (root / "xboxrecomp/src/nv2a_vk/nv2a_vk.c").read_text()
a = source.index("static uint32_t swizzle_index(")
b = source.index('#ifdef __ANDROID__\n#include "nv2a_vk_indexed.h"', a)
c = source.index("static void tex_free(", b)
(out / "indexed_swizzle.h").write_text(source[a:b])
(out / "indexed_decoder.h").write_text(source[b:c])
command = shlex.split(args.cc) + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
    "-fstack-protector-strong", "-I" + str(root / "xboxrecomp/src/nv2a_vk"), "-I" + str(out)]
if args.sanitize:
    command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
if args.scalar:
    command += ["-DNV2A_VK_INDEXED_NO_NEON=1"]
if args.non_android:
    command += ["-DINDEXED_TEST_NON_ANDROID=1"]
binary = out / "vulkan-indexed-textures"
subprocess.run(command + [str(root / "tests/vulkan_indexed_textures.c"), "-o", str(binary)], check=True)
if not args.compile_only:
    subprocess.run([str(binary)], check=True)
    if not args.non_android:
        for value, prop, expected in [(None, None, 1), (None, "", 1), (None, "1", 1),
                (None, "0", 0), (None, "invalid", 0), (None, "01", 0), ("", "0", 1),
                ("0", "1", 0), ("1", "0", 1), ("invalid", "1", 0)]:
            env = os.environ.copy()
            for key, setting in (("RECOMP_VK_INDEXED_TILES", value), ("P8_TEST_PROPERTY", prop)):
                env.pop(key, None)
                if setting is not None: env[key] = setting
            subprocess.run([str(binary), "--selector", str(expected)], env=env, check=True)
        print("Indexed selector: default, rollback, exact values, environment precedence and cached dispatch: PASS")
