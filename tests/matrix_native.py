#!/usr/bin/env python3
"""Build actual lifted matrix C + scalar helpers against native ARM64 code."""
import argparse
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', default='cc')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--sanitize', action='store_true')
p.add_argument('--generated-dir', type=Path, default=Path('android/app/build/generated/recomp'))
a = p.parse_args()
pattern = re.compile(r'void sub_00169F4A(?:_gen)?\(void\)\n\{.*?\n\}', re.S)
for f in sorted(a.generated_dir.glob('recomp_*.c')):
    match = pattern.search(f.read_text())
    if match:
        body = re.sub(r'sub_00169F4A(?:_gen)?', 'matrix_reference', match.group())
        break
else:
    raise SystemExit('Generate recomp sources first')
# Force the existing portable helpers in the reference TU, even on Android.
header = Path('xboxrecomp/templates/runtime/recomp_types.h').read_text()
header = header.replace('#if defined(__ANDROID__) && defined(__aarch64__)', '#if 0')
header = header.replace('#if !defined(__ANDROID__) || !defined(__aarch64__)', '#if 1')
ref_header = a.output.resolve().with_suffix('.scalar.h')
ref_header.write_text(header)
ref = a.output.resolve().with_suffix('.reference.c')
ref.write_text(f'#define RECOMP_GENERATED_CODE\n#include "{ref_header}"\n' + body)
cmd = [a.cc, '-std=c11', '-O2', '-ffp-contract=off', '-fno-strict-aliasing',
       '-Wall', '-Wextra', '-Werror', '-Wno-unused-label',
       '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-but-set-variable',
       'tests/matrix_native.c', str(ref), '-lm', '-o', str(a.output)]
if a.sanitize:
    cmd += ['-fsanitize=address,undefined']
subprocess.run(cmd, check=True)
