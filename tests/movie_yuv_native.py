#!/usr/bin/env python3
"""Build a differential test against the current generated Xbox function.

python3 tests/movie_yuv_native.py --sanitize --output /tmp/nfsu-yuv-test
/tmp/nfsu-yuv-test
Use --cc <NDK aarch64-linux-android30-clang> for an Android executable.
"""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--generated-dir", type=Path, default=Path("android/app/build/generated/recomp"))
parser.add_argument("--cc", default="cc")
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
pattern = re.compile(r"void sub_0019D840(?:_gen)?\(void\)\n\{.*?\n\}", re.S)
for path in sorted(args.generated_dir.glob("recomp_*.c")):
    match = pattern.search(path.read_text())
    if match:
        reference = re.sub(r"sub_0019D840(?:_gen)?", "movie_yuv_reference", match.group())
        break
else:
    raise SystemExit("No generated sub_0019D840 found; run generateRecomp first")
reference_path = args.output.resolve().with_suffix(".reference.h")
reference_path.write_text(reference + "\n")
command = [args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
           "-Wno-unused-label", "-Wno-unused-variable", "-Wno-unused-but-set-variable",
           f'-DMOVIE_YUV_REFERENCE="{reference_path}"', "tests/movie_yuv_native.c",
           "-o", str(args.output)]
if args.sanitize:
    command += ["-fsanitize=address,undefined"]
subprocess.run(command, check=True)
