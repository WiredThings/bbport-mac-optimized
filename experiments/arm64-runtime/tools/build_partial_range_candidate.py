"""Experimental range-guard control; not enabled in a playable component."""
from pathlib import Path
import hashlib,json,shlex,subprocess,time
from qa_build_guard import require_no_games
P=Path(__file__).resolve().parents[1];M=P/'vendor/metal_api'
B=Path('/private/tmp/blood-echoes-arm64-metal-20261023/build')
require_no_games();e=P/'evidence'/('partial-range-candidate-'+str(time.time_ns()));e.mkdir()
base=json.loads((P/'evidence/first-upload-candidate-current.json').read_text())
library=P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.first-upload-candidate.dylib'
def sha(f):return hashlib.file_digest(Path(f).open('rb'),'sha256').hexdigest()
assert sha(library)==base['sha256']
source=Path(base['evidence'])/'texture_cache.cpp';before=source.read_text()
old='// Only consider images that match base address.\n        // TODO: Maybe also consider subresources\n        if (image.info.guest_address != address) {'
new=old.replace('image.info.guest_address != address','image.info.guest_address != address || image.info.guest_size > max_size')
assert before.count(old)==1
fixed=e/'texture_cache.cpp';fixed.write_text(before.replace(old,new))
commands=subprocess.check_output(['ninja','-C',str(B),'-t','commands'],text=True).splitlines()
rel='shadps4/video_core/texture_cache/texture_cache.cpp'
cmd=next(shlex.split(x)for x in commands if x.startswith(('/usr/bin/c++','/usr/bin/clang++'))and x.endswith('/'+rel))
old='/Users/k.d./Documents/Codex/2026-10-16/https-github-com-loreanxavier-pt-pc/work/bbport-mac-optimized/engines/metal/gpu'
cmd=[x.replace(old,str(M))for x in cmd];cmd[1:1]=['-I'+str(P/'src'),'-I'+str(M/'shadps4/video_core/texture_cache')]
obj=e/'texture_cache.o';cmd[cmd.index('-o')+1]=str(obj);cmd[cmd.index('-MF')+1]=str(obj.with_suffix('.d'));cmd[-1]=str(fixed)
with(e/'build.log').open('w')as log:
    subprocess.run(cmd,cwd=B,stdout=log,stderr=subprocess.STDOUT,check=True)
    link=list(base['link']);indices=[i for i,x in enumerate(link)if x==base['compile'][base['compile'].index('-o')+1]];assert len(indices)==1
    link[indices[0]]=str(obj);out=library.with_name('libbbgpu.partial-range-candidate.dylib');link[link.index('-o')+1]=str(out)
    subprocess.run(link,cwd=B,stdout=log,stderr=subprocess.STDOUT,check=True)
def deps(f):return[x.strip().split(' (compatibility')[0]for x in subprocess.check_output(['otool','-L',str(f)],text=True).splitlines()[1:]]
expected={Path(x).name:x for x in deps(library)}
for x in deps(out):
    replacement=expected.get(Path(x).name)
    if replacement and replacement!=x:subprocess.run(['install_name_tool','-change',x,replacement,str(out)],check=True)
subprocess.run(['install_name_tool','-id',str(out),str(out)],check=True)
subprocess.run(['codesign','--force','--sign','-',str(out)],check=True)
assert sha(library)==base['sha256']
r=dict(evidence=str(e),candidate=str(out),sha256=sha(out),baseline_sha256=base['sha256'],source_baseline_sha256=sha(source),source_fixed_sha256=sha(fixed),compile=cmd,link=link,changed_objects=1,experimental_range_guard_only=True,formal_library_changed=False)
(e/'build.json').write_text(json.dumps(r,indent=2)+'\n');(P/'evidence/partial-range-candidate-current.json').write_text(json.dumps(r,indent=2)+'\n');print(e)
