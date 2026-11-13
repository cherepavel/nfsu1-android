#!/usr/bin/env python3
"""Compare the real device-bus translation unit at O0/O2 and replay real APU RAM access."""
import argparse
import filecmp
import os
from pathlib import Path
import shlex
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc',default='cc')
p.add_argument('--output-dir',type=Path,default=Path('/tmp/nfsu-devbus'))
p.add_argument('--sanitize',action='store_true')
p.add_argument('--compile-only',action='store_true')
p.add_argument('--replay',action='store_true')
a=p.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
cc=shlex.split(a.cc)
flags=['-std=gnu11','-ffp-contract=off','-ffunction-sections','-fdata-sections',
    '-fstack-protector-strong','-I'+str(root/'xboxrecomp/src'),'-pthread']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
link=['-Wl,-dead_strip' if sys.platform=='darwin' and 'android' not in a.cc else '-Wl,--gc-sections','-lm']
for opt in ('O0','O2'):
    obj=out/('devbus-'+opt+'.o')
    subprocess.run(cc+flags+['-'+opt,'-c',str(root/'xboxrecomp/src/kernel/xbox_devbus.c'),'-o',str(obj)],check=True)
    name=('apu-devbus-' if a.replay else 'devbus-')+opt
    source='apu_vp_cpu.c' if a.replay else 'devbus_cpu.c'
    extra=['-DAPU_REPLAY_REAL_DEVBUS=1','-DAPU_FILTER_NEON_TEST=1'] if a.replay else ['-Wall','-Wextra','-Werror','-Wno-comment']
    subprocess.run(cc+flags+['-O2']+extra+[str(root/'tests'/source),str(obj)]+link+['-o',str(out/name)],check=True)
    if not a.compile_only:
        if a.replay:
            for mix in ('0','1'):
                subprocess.run([str(out/name),str(out/f'pcm-{opt}-{mix}.bin')],env={**os.environ,
                    'RECOMP_APU_MIXDOWN_ALL':mix,'RECOMP_APU_FILTER_NEON':'1'},check=True)
        else:subprocess.run([str(out/name)],check=True)
if a.replay and not a.compile_only:
    for mix in ('0','1'):assert filecmp.cmp(out/f'pcm-O0-{mix}.bin',out/f'pcm-O2-{mix}.bin',shallow=False)
    print('PASS: real device-bus O0/O2 replay matches samples, bins, filters, guest voice RAM and PCM for low/physical aliases and both downmix policies')
