#!/usr/bin/env python3
"""Rebuild only the private native frontend's heap bridge, reuse fixed baseline objects.
Build-time baseline objects only; installed binaries never reference the old source tree.
"""
from pathlib import Path
import shlex,subprocess,json,os,hashlib
P=Path(__file__).resolve().parents[1];B=Path('/private/tmp/blood-echoes-arm64-metal-20261023/build');M=P/'vendor/metal_api';old='/Users/k.d./Documents/Codex/2026-10-16/https-github-com-loreanxavier-pt-pc/work/bbport-mac-optimized/engines/metal/gpu'
commands=subprocess.check_output(['ninja','-C',str(B),'-t','commands'],text=True).splitlines()
objects={}
for source in ['shadps4/video_core/rasterizer_core_pipe.cpp','shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp','shadps4/video_core/renderer_metal/mtl_rasterizer.mm','shadps4/video_core/renderer_metal/mtl_memory_stats.mm','shim/bbport_resource_lifetime.cpp','shim/bbport_heap_sites.cpp','shadps4/common/path_util.cpp','shim/bbgpu.cpp','shim/bbport_guest_hooks.cpp','shadps4/video_core/texture_cache/texture_cache.cpp','shim/remote/remote_front.cpp','shim/remote/remote_back.cpp','shadps4/video_core/renderer_metal/mtl_shader.mm','shadps4/video_core/renderer_metal/mtl_pso_cache.mm','shadps4/video_core/renderer_metal/mtl_scheduler.mm','shadps4/core/libraries/videoout/driver.cpp']:
 compile=next(shlex.split(l) for l in commands if l.startswith(('/usr/bin/c++','/usr/bin/clang++')) and l.endswith('/'+source))
 obj=P/'build'/('native-'+Path(source).stem+'.o');deps=obj.with_suffix('.d')
 compile=[x.replace(old,str(M)) for x in compile]
 compile[1:1]=['-I'+str(P/'src')]
 compile[compile.index('-o')+1]=str(obj);compile[compile.index('-MF')+1]=str(deps)
 subprocess.run(compile,cwd=B,check=True);objects['CMakeFiles/bbgpu.dir/'+source+'.o']=str(obj)
link=shlex.split(commands[-1].split('&&')[1].strip());out=P/'build/libbbgpu-heap-prototype.dylib'
link[link.index('-o')+1]=str(out)
for i,x in enumerate(link):
 if x in objects:link[i]=objects[x]
 elif not x.startswith('-') and (B/x).is_file():link[i]=str(B/x)
subprocess.run(link,cwd=B,check=True)
dest=P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib'
def dylibs(path):return [l.strip().split(' (compatibility')[0] for l in subprocess.check_output(['otool','-L',str(path)],text=True).splitlines()[1:]]
expected={Path(x).name:x for x in dylibs(dest)}
for x in dylibs(out):
 replacement=expected.get(Path(x).name)
 if replacement and replacement!=x:subprocess.run(['install_name_tool','-change',x,replacement,str(out)],check=True)
subprocess.run(['codesign','--force','--sign','-',str(out)],check=True)
backup=dest.with_name('libbbgpu.pre-heap-bridge.dylib')
if not backup.exists():subprocess.run(['/bin/cp','-c',str(dest),str(backup)],check=True)
os.replace(out,dest)
(P/'evidence/heap-bridge-build.json').write_text(json.dumps(dict(compile=compile,link=link,sha256=hashlib.file_digest(dest.open('rb'),'sha256').hexdigest()),indent=2))
print('Private native heap bridge rebuilt:',dest)
