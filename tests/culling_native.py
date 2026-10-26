#!/usr/bin/env python3
import argparse,pathlib,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--cc',default='cc');p.add_argument('--output',required=True,type=pathlib.Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
for f in pathlib.Path('android/app/build/generated/recomp').glob('recomp_*.c'):
 m=re.search(r'void sub_00018C30(?:_gen)?\(void\)\n\{.*?\n\}',f.read_text(),re.S)
 if m:break
else:raise SystemExit('missing generated reference')
ref=a.output.resolve().with_suffix('.reference.c');header=pathlib.Path('xboxrecomp/templates/runtime/recomp_types.h').resolve();ref.write_text('#define RECOMP_GENERATED_CODE\n#include "'+str(header)+'"\nvoid sub_00018B40(void);\n'+re.sub(r'sub_00018C30(?:_gen)?','culling_reference',m[0]))
cmd=[a.cc,'-std=c11','-O2','-ffp-contract=off','-fno-strict-aliasing','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-label','-Wno-unused-variable','-Wno-unused-but-set-variable','tests/culling_native.c',str(ref),'-lm','-o',str(a.output)]
if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
subprocess.run(cmd,check=True)
