#!/usr/bin/env python3
"""Build production VP replay at O0/O2; optionally compare on the host.

python3 tests/apu_vp_cpu.py --output-dir /tmp/nfsu-apu --sanitize --run
For Android pass --cc /path/to/aarch64-linux-android30-clang, then run both
executables on the device with output file arguments and compare those files.
The 'bench' argument runs without replay file I/O.
"""
import argparse
import filecmp
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', default='cc')
p.add_argument('--output-dir', type=Path, required=True)
p.add_argument('--sanitize', action='store_true')
p.add_argument('--run', action='store_true')
a = p.parse_args()
a.output_dir.mkdir(parents=True, exist_ok=True)
darwin = sys.platform == 'darwin' and 'android' not in Path(a.cc).name
for opt in ('O0', 'O2'):
    binary = a.output_dir / ('apu-' + opt)
    cmd = [a.cc, '-' + opt, '-std=gnu11', '-ffp-contract=off',
           '-ffunction-sections', '-fdata-sections', '-Ixboxrecomp/src',
           'tests/apu_vp_cpu.c',
           '-Wl,-dead_strip' if darwin else '-Wl,--gc-sections',
           '-pthread', '-lm', '-o', str(binary)]
    if a.sanitize:
        cmd += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    subprocess.run(cmd, check=True)
    if a.run:
        for mixdown in ('0', '1'):
            subprocess.run([str(binary.resolve()), str((a.output_dir / (opt + '-' + mixdown + '.bin')).resolve())],
                           env={**os.environ, 'RECOMP_APU_MIXDOWN_ALL': mixdown}, check=True)
if a.run:
    for mixdown in ('0', '1'):
        if not filecmp.cmp(a.output_dir / ('O0-' + mixdown + '.bin'), a.output_dir / ('O2-' + mixdown + '.bin'), shallow=False):
            raise SystemExit('FAIL: O0/O2 replay differs, mixdown=' + mixdown)
    print('PASS: O0/O2 samples, PCM output, filter state and guest voice RAM are byte-identical with both mixdown policies')
