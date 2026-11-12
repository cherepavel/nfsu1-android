#!/usr/bin/env python3
"""Build Android runtime regressions and a before/after shared-library probe."""
import argparse
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", required=True, help="NDK aarch64-linux-android30-clang")
parser.add_argument("--reference", type=Path, help="saved guest_fiber.c before the change")
parser.add_argument("--output-dir", type=Path, default=Path("/tmp/nfsu-preempt-leaf"))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
base = shlex.split(args.cc) + ["-O2", "-Wall", "-Wextra", "-Werror", "-Wno-comment",
    "-Wno-unused-parameter", "-fPIC", "-fstack-protector-strong", "-D_FORTIFY_SOURCE=2",
    "-funwind-tables", "-ffunction-sections", "-fdata-sections", "-DRECOMP_REGISTER_BLOCK=1",
    "-DRECOMP_EXECUTOR_TLS_ARG=1", "-Ixboxrecomp/src", "-Ixboxrecomp/src/kernel", "-Itests"]
common = ["xboxrecomp/src/kernel/guest_fiber.c", "xboxrecomp/src/kernel/guest_fiber_switch.S",
    "xboxrecomp/src/kernel/recomp_context.c", "xboxrecomp/src/platform/win32_compat.c"]
dispatcher = ["xboxrecomp/src/kernel/guest_fiber_dispatcher.c",
    "xboxrecomp/src/kernel/guest_dispatcher.c", "xboxrecomp/src/kernel/guest_scheduler.c"]
kernel = ["xboxrecomp/src/kernel/kernel_rtl.c", "xboxrecomp/src/kernel/kernel_hal.c",
    "xboxrecomp/src/kernel/kernel_thread.c", "xboxrecomp/src/kernel/kernel_sync.c"]
for name, sources in [("guest_fiber", common), ("guest_executor", common + dispatcher),
        ("guest_kernel_executor", common + dispatcher + kernel),
        ("guest_preempt_tls", ["xboxrecomp/src/kernel/recomp_context.c"])]:
    # The bridge fixture includes the entire pre-existing kernel_bridge.c.
    warnings = ["-Wno-error", "-DNFSU_GUEST_EXECUTOR=1", "-DNFSU_GUEST_SCHEDULER_TEST=1"] if name == "guest_kernel_executor" else []
    subprocess.run(base + warnings + ["tests/" + name + ".c"] + sources +
        ["-Wl,--gc-sections", "-pthread", "-lm", "-o", str(out / name)], cwd=root, check=True)
if args.reference:
    fixture = (root / "tests/guest_preempt_tls.c").read_text().replace(
        '"../xboxrecomp/src/kernel/guest_fiber.c"', '"' + str(args.reference.resolve()) + '"')
    reference_test = out / "query_reference.c"
    reference_test.write_text(fixture)
    subprocess.run(base + [str(reference_test), "xboxrecomp/src/kernel/recomp_context.c",
        "-Wl,--gc-sections", "-pthread", "-lm", "-o", str(out / "guest_preempt_reference")],
        cwd=root, check=True)
    for mode, source in [("reference", str(args.reference.resolve())), ("candidate", common[0])]:
        subprocess.run(base + ["-shared", "tests/guest_preempt_leaf_bench.c", source] + common[1:] +
            ["-Wl,--gc-sections", "-pthread", "-lm", "-o", str(out / ("libpreempt-" + mode + ".so"))],
            cwd=root, check=True)
    subprocess.run(base + ["-DNFSU_PREEMPT_LOADER=1", "tests/guest_preempt_leaf_bench.c", "-ldl",
        "-o", str(out / "preempt-leaf-bench")], cwd=root, check=True)
print("Built Android runtime checks" + (" and shared-library benchmark" if args.reference else ""))
