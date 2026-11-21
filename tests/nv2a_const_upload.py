#!/usr/bin/env python3
"""Compare production bulk constant packets to their retained scalar path."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="cc")
parser.add_argument("--output-dir", type=Path, default=Path("/tmp/nfsu-const-upload"))
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--compile-only", action="store_true")
parser.add_argument("--scalar", action="store_true", help="test portable helper even on ARM64")
parser.add_argument("--production-object", type=Path, help="APK build's nv2a_pb_exec.c.o for a device benchmark")
parser.add_argument("--objcopy", help="NDK llvm-objcopy, required with --production-object")
args = parser.parse_args()
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
source = (root / "xboxrecomp/src/kernel/nv2a_pb_exec.c").read_text()
for name, start, end in [
    ("const_packet_under_test.h", "uint32_t nv2a_pb_exec_run(", "/* Find where the title"),
    ("const_selector_under_test.h", "static int pb_const_bulk_enabled(void)", "void nv2a_pb_exec_report(void);"),
    ("const_state_under_test.h", "static struct {\n    uint32_t prog[VP_SLOTS][4];", "typedef struct { float pos[4]")]:
    a = source.index(start)
    b = source.index(end, a)
    (out / name).write_text(source[a:b])
binary = out / "nv2a-const-upload"
command = shlex.split(args.cc) + ["-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
    "-fstack-protector-strong", "-I" + str(root / "xboxrecomp/src/kernel"), "-I" + str(out)]
if args.sanitize:
    command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
if args.scalar:
    command += ["-DNV2A_CONST_PORTABLE=1"]
command += [str(root / "tests/nv2a_const_upload.c"), "-o", str(binary)]
subprocess.run(command, check=True)
if args.production_object:
    if not args.objcopy:
        parser.error("--production-object requires --objcopy")
    probe = out / "packet-production.o"
    subprocess.run([args.objcopy, "--globalize-symbol=s_vp", "--globalize-symbol=s_reg",
        "--globalize-symbol=pb_const_bulk_enabled.enabled", str(args.production_object.resolve()), str(probe)], check=True)
    state = (out / "const_state_under_test.h").read_text().split(" = {")[0].replace("static struct", "extern struct", 1)
    (out / "extern_const_state.h").write_text(state + ";\n")
    subprocess.run(shlex.split(args.cc) + ["-O2", "-fPIC", "-fstack-protector-strong",
        "-ffunction-sections", "-fdata-sections", "-I" + str(root / "xboxrecomp/src/kernel"),
        "-I" + str(out), str(root / "tests/nv2a_const_packet_bench.c"), str(probe),
        "-Wl,--gc-sections", "-pthread", "-lm", "-o", str(out / "nv2a-const-packet-bench")], check=True)
if not args.compile_only:
    subprocess.run([str(binary)], check=True)
    for environment, prop, expected in [(None, None, 1), (None, "", 1), (None, "0", 0),
            (None, "1", 1), (None, "invalid", 0), (None, "01", 0), ("", "0", 1),
            ("0", "1", 0), ("1", "0", 1), ("invalid", "1", 0)]:
        env = os.environ.copy()
        for key, value in (("RECOMP_PB_CONST_BULK", environment), ("PBCONST_TEST_PROPERTY", prop)):
            env.pop(key, None)
            if value is not None:
                env[key] = value
        subprocess.run([str(binary), "--selector", str(expected)], env=env, check=True)
    print("Constant packet startup: defaults, rollback, invalid values, cached selection and environment precedence: PASS")
