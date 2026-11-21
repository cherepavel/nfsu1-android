#!/usr/bin/env python3
"""Check production peak selection/reduction and VP/PCM/diagnostic identity."""
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
p.add_argument('--output-dir',type=Path,default=Path('/tmp/nfsu-apu-peaks'))
p.add_argument('--sanitize',action='store_true')
p.add_argument('--compile-only',action='store_true')
p.add_argument('--replay',action='store_true')
p.add_argument('--scalar',action='store_true')
a=p.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
cc=shlex.split(a.cc)
flags=['-O2','-std=gnu11','-ffp-contract=off','-ffunction-sections','-fdata-sections',
    '-fstack-protector-strong','-I'+str(root/'xboxrecomp/src'),
    '-I'+str(root/'xboxrecomp/src/apu'),'-I'+str(out),'-pthread']
if a.scalar:flags+=['-DAPU_PEAK_NO_NEON=1']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
link=['-Wl,-dead_strip' if sys.platform=='darwin' and 'android' not in a.cc else '-Wl,--gc-sections','-lm']
source=(root/'xboxrecomp/src/apu/apu_vp.c').read_text()
start=source.index('static int apu_peak_optimized(');end=source.index('/* #define DEBUG_MCPX */',start)
(out/'peak_selector.h').write_text(source[start:end])
name='apu-peak-replay' if a.replay else 'apu-peak'
if a.replay:
    obj=out/'devbus-O2.o'
    subprocess.run(cc+flags+['-c',str(root/'xboxrecomp/src/kernel/xbox_devbus.c'),'-o',str(obj)],check=True)
    inputs=['-DAPU_REPLAY_REAL_DEVBUS=1','-DAPU_FILTER_NEON_TEST=1','-DAPU_PEAK_TEST=1',
        '-DAPU_REPLAY_PEAK=1',str(root/'tests/apu_vp_cpu.c'),str(obj)]
else:inputs=['-Wall','-Wextra','-Werror',str(root/'tests/apu_peak.c')]
subprocess.run(cc+flags+inputs+link+['-o',str(out/name)],check=True)
if a.compile_only:sys.exit(0)
if a.replay:
    for trace in (None,'','0','1'):
        env={**os.environ};env.pop('RECOMP_APU_TRACE',None)
        if trace is not None:env['RECOMP_APU_TRACE']=trace
        subprocess.run([str(out/name),'--trace-check',str(int(trace is not None))],env=env,check=True)
    for mix in ('0','1'):
        for trace in (None,'1'):
            env={**os.environ,'RECOMP_APU_MIXDOWN_ALL':mix,'RECOMP_APU_FILTER_NEON':'1'}
            env.pop('RECOMP_APU_TRACE',None)
            if trace is not None:env['RECOMP_APU_TRACE']=trace
            paths=[out/f'pcm-{mode}.bin' for mode in ('0','1')]
            for mode,path in zip(('0','1'),paths):
                subprocess.run([str(out/name),str(path)],env={**env,'RECOMP_APU_PEAK_OPT':mode},check=True)
            assert filecmp.cmp(*paths,shallow=False)
            for path in paths:path.unlink()
    print('PASS: production PCM, bins, filter states, voice RAM, mix peak and trace diagnostics match; both downmix policies, trace off/on; trace presence/caching preserved')
else:
    subprocess.run([str(out/name)],check=True)
    for value,prop,expected in [(None,None,1),(None,'',1),(None,'0',0),(None,'1',1),
            (None,'invalid',0),(None,'01',0),('','0',1),('0','1',0),('1','0',1),('invalid','1',0)]:
        env=os.environ.copy()
        for key,setting in [('RECOMP_APU_PEAK_OPT',value),('APUPEAK_TEST_PROPERTY',prop)]:
            env.pop(key,None)
            if setting is not None:env[key]=setting
        subprocess.run([str(out/name),'--selector',str(expected)],env=env,check=True)
    print('PASS: default, exact selector values, environment precedence and cached selection')
