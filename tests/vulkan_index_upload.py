#!/usr/bin/env python3
"""Compare fused Vulkan index uploads against the production legacy path."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="cc")
parser.add_argument("--output-dir", type=Path, default=Path("/tmp/nfsu-index-upload"))
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--compile-only", action="store_true")
args = parser.parse_args()
args.output_dir.mkdir(parents=True, exist_ok=True)
source = (root / "xboxrecomp/src/nv2a_vk/nv2a_vk.c").read_text()
start = source.index("static uint32_t *s_prim_idx;")
end = source.index("/* ── Vertices", start)
(args.output_dir / "legacy_indices_under_test.h").write_text(source[start:end])
start = source.index("static int index_fuse_enabled(void)")
end = source.index("static struct {", start)
(args.output_dir / "index_selector_under_test.h").write_text(source[start:end])
start = source.index("static struct {\n    int          on;")
end = source.index("static int s_inst_on", start)
match_start = source.index("static int pend_matches(", end)
match_end = source.index("/* Can b be held", match_start)
(args.output_dir / "index_instancing_under_test.h").write_text(
    source[start:end] + source[match_start:match_end])
binary = args.output_dir / "vulkan-index-upload"
command = shlex.split(args.cc) + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
    "-Wno-missing-field-initializers", "-I" + str(root / "third_party/SDL/src/video/khronos"),
    "-I" + str(root / "xboxrecomp/src/nv2a_vk"), "-I" + str(args.output_dir)]
if args.sanitize:
    command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
command += [str(root / "tests/vulkan_index_upload.c"), "-o", str(binary)]
subprocess.run(command, check=True)
if not args.compile_only:
    subprocess.run([str(binary)], check=True)
    cases = [(None, None, None, None, 1, 0), (None, "", None, "", 1, 0),
             (None, "0", None, "1", 0, 1), (None, "1", None, "0", 1, 0),
             (None, "2", None, "2", 0, 0), (None, "invalid", None, "invalid", 0, 0),
             ("", "0", "", "1", 1, 0), ("0", "1", "0", "1", 0, 0),
             ("1", "0", "1", "0", 1, 1), ("invalid", "1", "invalid", "1", 0, 0)]
    for fuse_env, fuse_prop, stats_env, stats_prop, fuse, stats in cases:
        env = os.environ.copy()
        for name, value in (("RECOMP_VK_INDEX_FUSE", fuse_env), ("INDEX_TEST_FUSE", fuse_prop),
                            ("RECOMP_VK_INDEX_STATS", stats_env), ("INDEX_TEST_STATS", stats_prop)):
            env.pop(name, None)
            if value is not None:
                env[name] = value
        subprocess.run([str(binary), "--selector", str(fuse), str(stats)], env=env, check=True)
    print("Index startup: default-on, rollback, statistics opt-in and environment precedence: PASS")
