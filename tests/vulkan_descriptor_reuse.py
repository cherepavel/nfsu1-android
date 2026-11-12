#!/usr/bin/env python3
"""Exercise persistent descriptor allocation through the production adapter."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="cc")
parser.add_argument("--output-dir", type=Path, default=Path("/tmp/nfsu-descriptor-reuse"))
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--gpu", action="store_true")
parser.add_argument("--shader", type=Path, help="compiled .comp SPIR-V for a local GPU run")
parser.add_argument("--compile-only", action="store_true")
args = parser.parse_args()
args.output_dir.mkdir(parents=True, exist_ok=True)
android = (root / "xboxrecomp/src/nv2a_vk/nv2a_vk_android.h").read_text()
start = android.index("static void android_descriptor_pool_ready(void)")
(args.output_dir / "descriptor_adapter_under_test.h").write_text(android[start:])
start = android.index("static void android_frame_reset(Frame *frame)")
end = android.index("static void VKAPI_PTR android_begin_rendering", start)
(args.output_dir / "descriptor_reset_under_test.h").write_text(android[start:end])
source = (root / "xboxrecomp/src/nv2a_vk/nv2a_vk.c").read_text()
start = source.index("static int android_descriptor_reuse_requested(void)")
end = source.index("\n#endif", start)
(args.output_dir / "descriptor_selector_under_test.h").write_text(source[start:end])
binary = args.output_dir / ("descriptor-reuse-gpu" if args.gpu else "descriptor-reuse")
command = shlex.split(args.cc) + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
    "-Wno-missing-field-initializers", "-I" + str(root / "third_party/SDL/src/video/khronos"),
    "-I" + str(root / "xboxrecomp/src/nv2a_vk"), "-I" + str(args.output_dir)]
if args.sanitize:
    command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
name = "vulkan_descriptor_reuse_gpu.c" if args.gpu else "vulkan_descriptor_reuse.c"
command += [str(root / "tests" / name), "-o", str(binary)]
if args.gpu:
    command += ["-lvulkan"]
subprocess.run(command, check=True)
if not args.compile_only:
    if args.gpu and not args.shader:
        parser.error("--gpu requires --shader or --compile-only")
    subprocess.run([str(binary)] + ([str(args.shader)] if args.gpu else []), check=True)
    if not args.gpu:
        for environment, prop, expected in [(None, None, 1), (None, "", 1), (None, "0", 0),
                (None, "1", 1), (None, "invalid", 0), ("", "0", 1), ("0", "1", 0),
                ("1", "0", 1), ("invalid", "1", 0)]:
            env = os.environ.copy()
            for key, value in (("RECOMP_VK_DESCRIPTOR_REUSE", environment), ("REUSE_TEST_PROPERTY", prop)):
                env.pop(key, None)
                if value is not None: env[key] = value
            subprocess.run([str(binary), "--selector", str(expected)], env=env, check=True)
        print("Descriptor allocation reuse: startup defaults, rollback and environment precedence: PASS")
