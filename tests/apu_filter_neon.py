#!/usr/bin/env python3
"""Check paired SVF bit identity, selection, and complete VP/PCM replay."""
import argparse
import filecmp
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', default='cc')
p.add_argument('--output-dir', type=Path, default=Path('/tmp/nfsu-apu-filter'))
p.add_argument('--sanitize', action='store_true')
p.add_argument('--compile-only', action='store_true')
p.add_argument('--replay', action='store_true')
p.add_argument('--scalar', action='store_true')
a = p.parse_args(); out = a.output_dir.resolve(); out.mkdir(parents=True, exist_ok=True)
state = (root/'xboxrecomp/src/apu/apu_state.h').read_text()
start = state.index('#define flush_to_zero'); end = state.index('/* ============================================================', start)
(out/'filter_scalar.h').write_text(state[start:end])
source = (root/'xboxrecomp/src/apu/apu_vp.c').read_text()
start = source.index('static int apu_filter_neon_enabled('); end = source.index('static int apu_peak_optimized(',start)
(out/'filter_selector.h').write_text(source[start:end])
cmd = shlex.split(a.cc)+['-O2','-std=gnu11','-ffp-contract=off','-fstack-protector-strong',
    '-ffunction-sections','-fdata-sections','-I'+str(root/'xboxrecomp/src'),
    '-I'+str(root/'xboxrecomp/src/apu'),'-I'+str(out)]
if a.scalar: cmd+=['-DAPU_FILTER_NO_NEON=1']
if a.sanitize: cmd+=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
name = 'apu-filter-replay' if a.replay else 'apu-filter'
if a.replay:
    cmd+=['-DAPU_FILTER_NEON_TEST=1',str(root/'tests/apu_vp_cpu.c')]
else:
    cmd+=['-Wall','-Wextra','-Werror',str(root/'tests/apu_filter_neon.c')]
darwin = sys.platform=='darwin' and 'android' not in a.cc
cmd+=['-Wl,-dead_strip' if darwin else '-Wl,--gc-sections','-pthread','-lm','-o',str(out/name)]
subprocess.run(cmd,check=True)
if not a.compile_only:
    if a.replay:
        for mix in ('0','1'):
            for mode in ('0','1'):
                subprocess.run([str(out/name),str(out/f'pcm-{mix}-{mode}.bin')],env={**os.environ,
                    'RECOMP_APU_MIXDOWN_ALL':mix,'RECOMP_APU_FILTER_NEON':mode},check=True)
            assert filecmp.cmp(out/f'pcm-{mix}-0.bin',out/f'pcm-{mix}-1.bin',shallow=False)
        print('PASS: scalar/paired production VP samples, bins, filter states, voice RAM and final PCM match for both downmix policies')
    else:
        subprocess.run([str(out/name)],check=True)
        for value,prop,expected in [(None,None,1),(None,'',1),(None,'0',0),(None,'1',1),
                (None,'invalid',0),(None,'01',0),('','0',1),('0','1',0),('1','0',1),('invalid','1',0)]:
            env=os.environ.copy()
            for key,setting in [('RECOMP_APU_FILTER_NEON',value),('APUFILTER_TEST_PROPERTY',prop)]:
                env.pop(key,None)
                if setting is not None:env[key]=setting
            subprocess.run([str(out/name),'--selector',str(expected)],env=env,check=True)
        print('PASS: APU filter defaults, rollback, capability, exact values, environment precedence and cached selection')
