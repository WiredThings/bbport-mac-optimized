#!/usr/bin/env python3
"""Uninstalled ARM64 diagnostic library; frozen baseline objects except four readers."""
from pathlib import Path
import json,shlex,subprocess,hashlib,time,sys
from native_observer import NativeObserver
from qa_memory_policy import MemoryPolicy
P=Path(__file__).resolve().parents[1];B=Path('/private/tmp/blood-echoes-arm64-metal-20261023/build');M=P/'vendor/metal_api'
e=Path('/private/tmp/blood-echoes-texture-diag-evidence').read_text().strip();e=Path(e);assert e.parent==P/'evidence'
base=json.loads((P/'evidence/heap-bridge-build.json').read_text());dest=P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib'
def sha(f):return hashlib.file_digest(Path(f).open('rb'),'sha256').hexdigest()
assert sha(dest)==base['sha256']
obs=NativeObserver();before=obs.system();assert not before['locked']and before['pressure']!=4
policy=MemoryPolicy(before);started=time.monotonic();steps=[]
def run(cmd,label,cwd=B):
 assert obs.path(12077) is None and obs.path(12080) is None,'do not compile while manual game runs'
 with(e/(label+'.log')).open('w')as log:
  proc=subprocess.Popen(cmd,cwd=cwd,stdout=log,stderr=subprocess.STDOUT)
  try:
   while proc.poll()is None:
    reason=policy.observe(time.monotonic()-started,obs.system())
    if reason:raise RuntimeError('compile guard: '+reason)
    time.sleep(.25)
   if proc.returncode:raise RuntimeError(label+' failed; inspect '+str(e/(label+'.log')))
  finally:
   if proc.poll()is None:
    proc.terminate()
    try:proc.wait(timeout=5)
    except subprocess.TimeoutExpired:proc.kill();proc.wait()
 steps.append({'label':label,'cmd':cmd});print(label+' passed',flush=True)
commands=subprocess.check_output(['ninja','-C',str(B),'-t','commands'],text=True).splitlines();link=list(base['link'])
sources=[('shadps4/video_core/frame_capture.cpp','frame_capture.cpp.o'),('shadps4/video_core/rasterizer_core.cpp','rasterizer_core.cpp.o'),('shadps4/video_core/rasterizer_core_pipe.cpp','native-rasterizer_core_pipe.o'),('shadps4/video_core/texture_cache/texture_cache.cpp','native-texture_cache.o')]
old='/Users/k.d./Documents/Codex/2026-10-16/https-github-com-loreanxavier-pt-pc/work/bbport-mac-optimized/engines/metal/gpu'
try:
 for n,(rel,suffix)in enumerate(sources):
  cmd=next(shlex.split(x)for x in commands if x.startswith(('/usr/bin/c++','/usr/bin/clang++'))and x.endswith('/'+rel))
  cmd=[x.replace(old,str(M))for x in cmd];cmd.insert(1,"-I"+str(P/"src"));obj=e/(Path(rel).stem+'.o');cmd[cmd.index('-o')+1]=str(obj);cmd[cmd.index('-MF')+1]=str(obj.with_suffix('.d'))
  run(cmd,'compile-'+str(n))
  hits=[i for i,x in enumerate(link)if x.endswith('/'+suffix)];assert len(hits)==1,(rel,hits);link[hits[0]]=str(obj)
 out=dest.with_name('libbbgpu.texture-candidate.dylib');link[link.index('-o')+1]=str(out);run(link,'link')
 def deps(f):return[x.strip().split(' (compatibility')[0]for x in subprocess.check_output(['otool','-L',str(f)],text=True).splitlines()[1:]]
 expected={Path(x).name:x for x in deps(dest)}
 for x in deps(out):
  replacement=expected.get(Path(x).name)
  if replacement and replacement!=x:run(['install_name_tool','-change',x,replacement,str(out)],'dep-'+Path(x).name)
 run(['install_name_tool','-id',str(out),str(out)],'install-id');run(['codesign','--force','--sign','-',str(out)],'sign');run(['codesign','--verify',str(out)],'verify')
 assert sha(dest)==base['sha256']
 result={'candidate':str(out),'sha256':sha(out),'baseline':base['sha256'],'baseline_unchanged':True,'steps':steps,'before':before,'after':obs.system(),'installed':False,'game_test':False,'cache_budgets_changed':False}
 (e/'build.json').write_text(json.dumps(result,indent=2)+'\n');print('UNINSTALLED TEXTURE CANDIDATE '+str(out),flush=True)
finally:obs.close()
