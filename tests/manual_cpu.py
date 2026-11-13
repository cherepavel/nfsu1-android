#!/usr/bin/env python3
"""Run production manual-wrapper fixtures at O0/O2 with both register ABIs."""
import argparse
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', default='cc')
p.add_argument('--output-dir', type=Path, default=Path('/tmp/nfsu-manual-cpu'))
p.add_argument('--sanitize', action='store_true')
p.add_argument('--compile-only', action='store_true')
a = p.parse_args()
out = a.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
android = 'android' in a.cc
flags = ['-std=gnu11', '-D__ANDROID__=1', '-DNFSU2_VULKAN=1',
         '-ffunction-sections', '-fdata-sections', '-fno-strict-aliasing',
         '-fwrapv', '-ffp-contract=off', '-pthread',
         '-I'+str(root/'src'), '-I'+str(root/'tests'),
         '-I'+str(root/'xboxrecomp/include'),
         '-I'+str(root/'android/app/build/generated/recomp')]
if sys.platform == 'darwin' and not android:
    # Only Android-only diagnostics use this syscall. No fixture calls it.
    flags += ['-DSYS_gettid=SYS_thread_selfid', '-Wno-deprecated-declarations']
    stub = out/'host-include/sys'
    stub.mkdir(parents=True, exist_ok=True)
    # Declarations only: unused Android property code must be dead-stripped.
    (stub/'system_properties.h').write_text(
        '#define PROP_VALUE_MAX 92\nint __system_property_get(const char *, char *);\n')
    flags += ['-I'+str(stub.parent)]
if a.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
link = ['-Wl,-dead_strip' if sys.platform == 'darwin' and not android
        else '-Wl,--gc-sections', '-lm']
suites = {
    'manual_native_wrappers': ['src/recomp_manual.c'],
    'gpu_flip_accounting': ['src/recomp_manual.c'],
    'options_blur': ['src/input_mode.c'],
    'port_credits': [],
    'controller_touch_options': ['src/input_mode.c', 'src/race_touch.c'],
    'race_hold_scanner': ['src/race_touch.c'],
    'race_control_features': ['src/race_touch.c'],
}
count = 0
for abi in ('legacy', 'block'):
    abi_flags = ['-DRECOMP_REGISTER_BLOCK=1', '-DRECOMP_EXECUTOR_TLS_ARG=1'] if abi == 'block' else []
    for opt in ('0', '2'):
        for name, extra in suites.items():
            binary = out/f'{name}-{abi}-O{opt}'
            command = shlex.split(a.cc)+flags+abi_flags+['-O'+opt,
                str(root/'tests'/f'{name}.c')]+[str(root/f) for f in extra]+link+['-o', str(binary)]
            subprocess.run(command, check=True)
            if not a.compile_only:
                print(f'{name}: {abi} O{opt}', flush=True)
                subprocess.run([str(binary)], check=True, cwd=out)
            count += 1
print(f'PASS: {count} '+('compiled fixtures' if a.compile_only else 'wrapper fixture runs'))
