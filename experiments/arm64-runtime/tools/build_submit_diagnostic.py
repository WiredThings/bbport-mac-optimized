"""Two-object, timing-only GPU diagnostic layered on the accepted first-upload fix."""
from pathlib import Path
import hashlib,json,plistlib,shlex,shutil,subprocess,time,sys
P=Path('/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype');sys.path.insert(0,str(P/'tools'))
from qa_build_guard import require_no_games
require_no_games();M=P/'vendor/metal_api';B=Path('/private/tmp/blood-echoes-arm64-metal-20261023/build');e=P/'evidence'/('submit-stage-build-'+str(time.time_ns()));e.mkdir()
base=json.loads((P/'evidence/first-upload-candidate-current.json').read_text());library=Path(base['candidate'])
def sha(f):return hashlib.file_digest(Path(f).open('rb'),'sha256').hexdigest()
assert sha(library)==base['sha256']
def replace(text,old,new):
 assert text.count(old)==1,old
 return text.replace(old,new)
original=(M/'shadps4/video_core/amdgpu/liverpool.cpp').read_text();text='#include "bb_stutter_audit.h"\n'+original
text=replace(text,'    if (EmulatorSettings.IsCopyGpuBuffers()) {\n        copy = std::make_shared<SubmittedCopy>();','    if (EmulatorSettings.IsCopyGpuBuffers()) {\n        BbAudit::Scope snapshot_audit{61,dcb.size_bytes()+ccb.size_bytes(),500000};\n        copy = std::make_shared<SubmittedCopy>();')
text=replace(text,'    auto prep_submission = draw_prep ? draw_prep->Build(dcb) : nullptr;','    auto prep_submission = [&] {\n        BbAudit::Scope preparation_audit{62,dcb.size_bytes(),500000};\n        return draw_prep ? draw_prep->Build(dcb) : nullptr;\n    }();')
text=replace(text,'        // Numbering, enqueueing and queueing under one lock keep the three orders identical.','        BbAudit::Scope queue_audit{63,0,500000};\n        // Numbering, enqueueing and queueing under one lock keep the three orders identical.')
start=text.index('void Liverpool::SubmitGfx(');end=text.index('\nvoid Liverpool::SignalDecodedFrames()',start);body=text[start:end]
body=replace(body,'    std::scoped_lock lk{submit_mutex};','    BbAudit::Scope notify_audit{64,0,500000};\n    std::scoped_lock lk{submit_mutex};');text=text[:start]+body+text[end:]
back=(M/'shim/remote/remote_back.cpp').read_text();back=replace(back,'std::span<const u32> InlineCommands(const u32* words, u32 count) {','std::span<const u32> InlineCommands(const u32* words, u32 count) {\n    BbAudit::Scope inline_audit{60,u64(count)*sizeof(u32),500000};')
sources={'shadps4/video_core/amdgpu/liverpool.cpp':text,'shim/remote/remote_back.cpp':back};commands=subprocess.check_output(['ninja','-C',str(B),'-t','commands'],text=True).splitlines();link=list(base['link']);built=[]
with(e/'build.log').open('w')as log:
 for rel,body in sources.items():
  local=e/Path(rel).name;local.write_text(body);cmd=next(shlex.split(x)for x in commands if x.startswith(('/usr/bin/c++','/usr/bin/clang++'))and x.endswith('/'+rel))
  old='/Users/k.d./Documents/Codex/2026-10-16/https-github-com-loreanxavier-pt-pc/work/bbport-mac-optimized/engines/metal/gpu';cmd=[x.replace(old,str(M))for x in cmd];cmd[1:1]=['-I'+str(P/'src'),'-I'+str((M/rel).parent)]
  obj=e/(Path(rel).stem+'.o');cmd[cmd.index('-o')+1]=str(obj);cmd[cmd.index('-MF')+1]=str(obj.with_suffix('.d'));cmd[-1]=str(local)
  subprocess.run(cmd,cwd=B,stdout=log,stderr=subprocess.STDOUT,check=True)
  indices=[i for i,x in enumerate(link)if x.endswith('/'+rel+'.o')or x.endswith('/native-'+Path(rel).stem+'.o')];assert len(indices)==1,(rel,indices)
  link[indices[0]]=str(obj);built.append(dict(source=rel,original_sha256=sha(M/rel),instrumented_sha256=sha(local),command=cmd))
 out=library.with_name('libbbgpu.submit-diagnostic.dylib');link[link.index('-o')+1]=str(out);subprocess.run(link,cwd=B,stdout=log,stderr=subprocess.STDOUT,check=True)
def deps(f):return[x.strip().split(' (compatibility')[0]for x in subprocess.check_output(['otool','-L',str(f)],text=True).splitlines()[1:]]
expected={Path(x).name:x for x in deps(library)}
for old in deps(out):
 new=expected.get(Path(old).name)
 if new and old!=new:subprocess.run(['install_name_tool','-change',old,new,str(out)],check=True)
subprocess.run(['install_name_tool','-id',str(out),str(out)],check=True);subprocess.run(['codesign','--force','--sign','-',str(out)],check=True)
meta=json.loads((P/'evidence/first-upload-gpu-app.json').read_text());app=P/'backend/bbport/bin'/('Bloodborne Metal Submit Timing QA '+str(time.time_ns())+'.app');shutil.copytree(Path(meta['target']).parents[2],app,symlinks=True);exe=app/'Contents/MacOS/bb-gpu'
subprocess.run(['install_name_tool','-change',str(library),str(out),str(exe)],check=True)
info=app/'Contents/Info.plist';data=plistlib.loads(info.read_bytes());data['CFBundleIdentifier']='io.github.bbport.optimized.submit-timing.'+str(time.time_ns());info.write_bytes(plistlib.dumps(data));subprocess.run(['codesign','--force','--sign','-',str(app)],check=True)
record=dict(evidence=str(e),library=str(out),library_sha256=sha(out),target=str(exe),original_target=meta['original_target'],timing_only=True,performance_acceptance=False,baseline_first_upload_sha256=base['sha256'],objects=built,link=link,kinds={'60':'remote_inline_retention','61':'submission_snapshot','62':'submission_preparation','63':'submission_queue','64':'submission_notify'})
(e/'result.json').write_text(json.dumps(record,indent=2)+'\n');(P/'evidence/submit-gpu-app.json').write_text(json.dumps(record,indent=2)+'\n');print(e)
