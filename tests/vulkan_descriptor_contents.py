#!/usr/bin/env python3
"""Check exact consecutive descriptor requests through the production adapter."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', default='cc')
p.add_argument('--output-dir', type=Path, default=Path('/tmp/nfsu-descriptor-contents'))
p.add_argument('--sanitize', action='store_true')
p.add_argument('--gpu', action='store_true')
p.add_argument('--shader', type=Path)
p.add_argument('--compile-only', action='store_true')
a = p.parse_args()
out = a.output_dir.resolve(); out.mkdir(parents=True, exist_ok=True)
source = (root/'xboxrecomp/src/nv2a_vk/nv2a_vk_android.h').read_text()
(out/'descriptor_adapter_under_test.h').write_text(source[source.index('static void android_descriptor_pool_ready(void)'):])
start = source.index('static void android_frame_reset(Frame *frame)')
(out/'descriptor_reset_under_test.h').write_text(source[start:source.index('static void VKAPI_PTR android_begin_rendering', start)])
source = (root/'xboxrecomp/src/nv2a_vk/nv2a_vk.c').read_text()
start = source.index('static int android_descriptor_contents_requested(void)')
(out/'descriptor_selector_under_test.h').write_text(source[start:source.index('\n#endif', start)])
binary = out/('descriptor-contents-gpu' if a.gpu else 'descriptor-contents')
command = shlex.split(a.cc)+['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
    '-Wno-missing-field-initializers', '-I'+str(root/'third_party/SDL/src/video/khronos'),
    '-I'+str(root/'xboxrecomp/src/nv2a_vk'), '-I'+str(out)]
if a.sanitize: command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
if a.gpu:
    command += ['-DTEST_DESCRIPTOR_CONTENTS=1', str(root/'tests/vulkan_descriptor_reuse_gpu.c'), '-lvulkan']
else: command += [str(root/'tests/vulkan_descriptor_contents.c')]
subprocess.run(command+['-o', str(binary)], check=True)
if not a.compile_only:
    if a.gpu and not a.shader: p.error('--gpu requires --shader or --compile-only')
    subprocess.run([str(binary)]+([str(a.shader)] if a.gpu else []), check=True)
    if not a.gpu:
        for value, prop, expected in [(None,None,1), (None,'',1), (None,'0',0),
                (None,'1',1), (None,'2',2), (None,'invalid',0), (None,'01',0),
                ('','0',1), ('0','1',0), ('1','0',1), ('2','0',2), ('invalid','1',0)]:
            env = os.environ.copy()
            for key, setting in [('RECOMP_VK_DESCRIPTOR_CONTENTS',value), ('CONTENTS_TEST_PROPERTY',prop)]:
                env.pop(key,None)
                if setting is not None: env[key] = setting
            subprocess.run([str(binary),'--selector',str(expected)],env=env,check=True)
        print('PASS: contents default, exact values, count-only, rollback and environment precedence')
