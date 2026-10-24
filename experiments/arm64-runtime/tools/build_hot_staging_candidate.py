#!/usr/bin/env python3
"""Build an uninstalled private Metal candidate with the current object overrides."""
from pathlib import Path
import hashlib,json,shlex,subprocess
P=Path(__file__).resolve().parents[1];B=Path('/private/tmp/blood-echoes-arm64-metal-20261023/build');M=P/'vendor/metal_api'
old='/Users/k.d./Documents/Codex/2026-10-16/https-github-com-loreanxavier-pt-pc/work/bbport-mac-optimized/engines/metal/gpu'
source='shadps4/video_core/renderer_metal/mtl_runtime.mm'
commands=subprocess.check_output(['ninja','-C',str(B),'-t','commands'],text=True).splitlines()
compile=next(shlex.split(l)for l in commands if l.startswith(('/usr/bin/c++','/usr/bin/clang++'))and l.endswith('/'+source))
compile=[x.replace(old,str(M))for x in compile];compile[1:1]=['-I'+str(P/'src')]
obj=P/'build/native-mtl_runtime-hot-staging.o';compile[compile.index('-o')+1]=str(obj);compile[compile.index('-MF')+1]=str(obj.with_suffix('.d'))
subprocess.run(compile,cwd=B,check=True)
base=json.loads((P/'evidence/heap-bridge-build.json').read_text());link=list(base['link']);indices=[i for i,x in enumerate(link)if x.endswith('mtl_runtime.mm.o')or x.endswith('native-mtl_runtime-hot-staging.o')];assert len(indices)==1;link[indices[0]]=str(obj)
dest=P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib';out=dest.with_name('libbbgpu.hot-staging-candidate.dylib');assert not out.exists()
link[link.index('-o')+1]=str(out);subprocess.run(link,cwd=B,check=True)
def deps(path):return [l.strip().split(' (compatibility')[0]for l in subprocess.check_output(['otool','-L',str(path)],text=True).splitlines()[1:]]
expected={Path(x).name:x for x in deps(dest)}
for x in deps(out):
 replacement=expected.get(Path(x).name)
 if replacement and replacement!=x:subprocess.run(['install_name_tool','-change',x,replacement,str(out)],check=True)
# Unique identity lets native tests load this candidate without replacing the live baseline.
subprocess.run(['install_name_tool','-id',str(out),str(out)],check=True);subprocess.run(['codesign','--force','--sign','-',str(out)],check=True)
record=dict(scope='uninstalled private candidate, dormant by default',compile=compile,link=link,candidate=str(out),sha256=hashlib.file_digest(out.open('rb'),'sha256').hexdigest(),baseline_sha256=hashlib.file_digest(dest.open('rb'),'sha256').hexdigest(),source_sha256=hashlib.file_digest((M/source).open('rb'),'sha256').hexdigest(),helper_sha256=hashlib.file_digest((P/'src/bb_staging_recent.h').open('rb'),'sha256').hexdigest())
(P/'evidence/hot-staging-candidate-build.json').write_text(json.dumps(record,indent=2)+'\n');print('Candidate only:',out,record['sha256'],flush=True)
