"""Bounded metadata-only audit of partial writes over GPU-authoritative images."""
from pathlib import Path
import hashlib,json,shlex,subprocess,time
from qa_build_guard import require_no_games
P=Path(__file__).resolve().parents[1];M=P/'vendor/metal_api'
B=Path('/private/tmp/blood-echoes-arm64-metal-20261023/build')
require_no_games();e=P/'evidence'/('partial-audit-candidate-'+str(time.time_ns()));e.mkdir()
base=json.loads((P/'evidence/first-upload-candidate-current.json').read_text())
library=P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.first-upload-candidate.dylib'
def sha(f):return hashlib.file_digest(Path(f).open('rb'),'sha256').hexdigest()
assert sha(library)==base['sha256']
source=Path(base['evidence'])/'texture_cache.cpp';before=source.read_text()
old='void TextureCache::InvalidateMemoryFromGPU(VAddr address, size_t max_size) {\n    std::scoped_lock lock{mutex};'
new=old+'\n    const bool partial_audit = BbAudit::Enabled();\n    if(partial_audit){\n        static std::atomic<bool> announced{false};\n        if(!announced.exchange(true))std::fputs("PARTIAL_IMAGE_AUDIT_ACTIVE limit=64 render_behavior_changed=0\\n",stderr);\n    }'
assert before.count(old)==1
fixed=e/'texture_cache.cpp'
text=before.replace(old,new)
needle='    ForEachImageInRegion(address, 1, [&](ImageId image_id, Image& image) {'
insert=needle+'\n        if(partial_audit && True(image.flags & ImageFlagBits::GpuModified) &&\n           ((max_size && image.Overlaps(address,max_size) &&\n             (image.info.guest_address!=address || max_size<image.info.guest_size)) ||\n            (!max_size && image.info.guest_address==address))){\n            static std::atomic<unsigned> printed{0};\n            if(printed.fetch_add(1)<64)std::fprintf(stderr,\n                "PARTIAL_IMAGE_AUDIT address=%llu bytes=%llu image=%llu image_bytes=%llu flags=%u tile=%u format=%u width=%u height=%u\\n",\n                (unsigned long long)address,(unsigned long long)max_size,\n                (unsigned long long)image.info.guest_address,(unsigned long long)image.info.guest_size,\n                u32(image.flags),u32(image.info.tile_mode),u32(image.info.pixel_format),image.info.size.width,image.info.size.height);\n        }'
assert text.count(needle)==1
fixed.write_text('#include "bb_stutter_audit.h"\n'+text.replace(needle,insert))
commands=subprocess.check_output(['ninja','-C',str(B),'-t','commands'],text=True).splitlines()
rel='shadps4/video_core/texture_cache/texture_cache.cpp'
cmd=next(shlex.split(x)for x in commands if x.startswith(('/usr/bin/c++','/usr/bin/clang++'))and x.endswith('/'+rel))
old='/Users/k.d./Documents/Codex/2026-10-16/https-github-com-loreanxavier-pt-pc/work/bbport-mac-optimized/engines/metal/gpu'
cmd=[x.replace(old,str(M))for x in cmd];cmd[1:1]=['-I'+str(P/'src'),'-I'+str(M/'shadps4/video_core/texture_cache')]
obj=e/'texture_cache.o';cmd[cmd.index('-o')+1]=str(obj);cmd[cmd.index('-MF')+1]=str(obj.with_suffix('.d'));cmd[-1]=str(fixed)
with(e/'build.log').open('w')as log:
    subprocess.run(cmd,cwd=B,stdout=log,stderr=subprocess.STDOUT,check=True)
    link=list(base['link']);indices=[i for i,x in enumerate(link)if x==base['compile'][base['compile'].index('-o')+1]];assert len(indices)==1
    link[indices[0]]=str(obj);out=library.with_name('libbbgpu.partial-audit-candidate.dylib');link[link.index('-o')+1]=str(out)
    subprocess.run(link,cwd=B,stdout=log,stderr=subprocess.STDOUT,check=True)
def deps(f):return[x.strip().split(' (compatibility')[0]for x in subprocess.check_output(['otool','-L',str(f)],text=True).splitlines()[1:]]
expected={Path(x).name:x for x in deps(library)}
for x in deps(out):
    replacement=expected.get(Path(x).name)
    if replacement and replacement!=x:subprocess.run(['install_name_tool','-change',x,replacement,str(out)],check=True)
subprocess.run(['install_name_tool','-id',str(out),str(out)],check=True)
subprocess.run(['codesign','--force','--sign','-',str(out)],check=True)
assert sha(library)==base['sha256']
r=dict(evidence=str(e),candidate=str(out),sha256=sha(out),baseline_sha256=base['sha256'],source_baseline_sha256=sha(source),source_fixed_sha256=sha(fixed),compile=cmd,link=link,changed_objects=1,diagnostic_only=True,render_behavior_changed=False,formal_library_changed=False)
(e/'build.json').write_text(json.dumps(r,indent=2)+'\n');(P/'evidence/partial-audit-candidate-current.json').write_text(json.dumps(r,indent=2)+'\n');print(e)
# A separate signed child app; the runner scopes its private alias and restores it.
import plistlib,shutil
meta=json.loads((P/'evidence/first-upload-gpu-app.json').read_text())
app=P/'backend/bbport/bin'/('Bloodborne Metal Partial Audit QA '+str(time.time_ns())+'.app')
shutil.copytree(Path(meta['target']).parents[2],app,symlinks=True);exe=app/'Contents/MacOS/bb-gpu'
subprocess.run(['install_name_tool','-change',str(library),str(out),str(exe)],check=True)
info=app/'Contents/Info.plist';data=plistlib.loads(info.read_bytes());data['CFBundleIdentifier']='io.github.bbport.optimized.partial-audit.'+str(time.time_ns());info.write_bytes(plistlib.dumps(data))
subprocess.run(['codesign','--force','--sign','-',str(app)],check=True)
(P/'evidence/partial-audit-gpu-app.json').write_text(json.dumps(dict(target=str(exe),original_target=meta['original_target'],library=str(out),library_sha256=sha(out),diagnostic_only=True),indent=2)+'\n')
