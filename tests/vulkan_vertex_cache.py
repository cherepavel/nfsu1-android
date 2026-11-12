#!/usr/bin/env python3
"""Exercise the production vertex upload path with a mock Vulkan recorder."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="cc")
parser.add_argument("--output-dir", type=Path, default=Path("/tmp/nfsu-vertex-cache"))
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--compile-only", action="store_true")
args = parser.parse_args()
args.output_dir.mkdir(parents=True, exist_ok=True)
source = (root / "xboxrecomp/src/nv2a_vk/nv2a_vk.c").read_text()
start = source.index("typedef struct { const uint8_t *lo, *hi; VkDeviceSize at; } VRun;")
end = source.index("/* ── Back end entry points", start)
(args.output_dir / "vertex_upload_under_test.h").write_text(
    '#include "nv2a_vk_vertex_cache.h"\n' + source[start:end])
selector_start = source.index("static int vertex_cache_mode(void)")
selector_end = source.index('#include "nv2a_vk_vertex_cache.h"', selector_start)
(args.output_dir / "vertex_selector_under_test.h").write_text(source[selector_start:selector_end])
binary = args.output_dir / "vulkan-vertex-cache"
command = shlex.split(args.cc) + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
    "-Wno-missing-field-initializers", "-ffp-contract=off",
    "-I" + str(root / "third_party/SDL/src/video/khronos"),
    "-I" + str(root / "xboxrecomp/src/nv2a_vk"), "-I" + str(args.output_dir)]
if args.sanitize:
    command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
command += [str(root / "tests/vulkan_vertex_cache.c"), "-o", str(binary)]
subprocess.run(command, check=True)
if not args.compile_only:
    subprocess.run([str(binary)], check=True)
    cases = [(None, None, 1), (None, "", 1), (None, "0", 0),
             (None, "1", 1), (None, "2", 2), (None, "invalid", 0),
             (None, "01", 0), ("", "0", 1), ("0", "1", 0),
             ("1", "0", 1), ("2", "0", 2), ("invalid", "1", 0)]
    for environment, property_value, expected in cases:
        env = os.environ.copy()
        for name, value in (("RECOMP_VK_VERTEX_CACHE", environment),
                            ("VKVTX_TEST_PROPERTY", property_value)):
            env.pop(name, None)
            if value is not None:
                env[name] = value
        subprocess.run([str(binary), "--selector", str(expected)], env=env, check=True)
    print("Vertex upload startup: default-on, rollback, measure mode and environment precedence: PASS")
