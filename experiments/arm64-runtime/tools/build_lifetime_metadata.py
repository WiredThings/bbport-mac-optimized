#!/usr/bin/env python3
"""Relink the existing private GPU front/back library with one lifetime source.
All other current private overrides remain from heap-bridge-build.json.
"""
from pathlib import Path
import hashlib,json,os,shlex,subprocess
P=Path(__file__).resolve().parents[1];B=Path('/private/tmp/blood-echoes-arm64-metal-20261023/build');M=P/'vendor/metal_api'
old='/Users/k.d./Documents/Codex/2026-10-16/https-github-com-loreanxavier-pt-pc/work/bbport-mac-optimized/engines/metal/gpu'
manifest=json.loads((P/'evidence/heap-bridge-build.json').read_text());link=manifest['link'];source='shim/bbport_resource_lifetime.cpp'
commands=subprocess.check_output(['ninja','-C',str(B),'-t','commands'],text=True).splitlines();compile=next(shlex.split(l)for l in commands if l.startswith(('/usr/bin/c++','/usr/bin/clang++'))and l.endswith('/'+source))
compile=[x.replace(old,str(M))for x in compile];compile[1:1]=['-I'+str(P/'src')];obj=P/'build/native-bbport_resource_lifetime.o';compile[compile.index('-o')+1]=str(obj);compile[compile.index('-MF')+1]=str(obj.with_suffix('.d'));subprocess.run(compile,cwd=B,check=True)
found=[i for i,x in enumerate(link)if x.endswith('bbport_resource_lifetime.cpp.o')or x.endswith('native-bbport_resource_lifetime.o')];assert len(found)==1;link[found[0]]=str(obj)
out=P/'build/libbbgpu-lifetime-metadata.dylib';link[link.index('-o')+1]=str(out);subprocess.run(link,cwd=B,check=True)
dest=P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib'
def dylibs(f):return [l.strip().split(' (compatibility')[0]for l in subprocess.check_output(['otool','-L',str(f)],text=True).splitlines()[1:]]
expected={Path(x).name:x for x in dylibs(dest)}
for x in dylibs(out):
 replacement=expected.get(Path(x).name)
 if replacement and replacement!=x:subprocess.run(['install_name_tool','-change',x,replacement,str(out)],check=True)
subprocess.run(['codesign','--force','--sign','-',str(out)],check=True)
backup=dest.with_name('libbbgpu.before-extra-live-metadata.dylib')
if not backup.exists():subprocess.run(['/bin/cp','-c',str(dest),str(backup)],check=True)
os.replace(out,dest);manifest.update(compile=compile,link=link,sha256=hashlib.sha256(dest.read_bytes()).hexdigest());(P/'evidence/heap-bridge-build.json').write_text(json.dumps(manifest,indent=2)+'\n');(P/'evidence/extra-live-metadata-build.json').write_text(json.dumps(manifest,indent=2)+'\n');print('Updated private lifetime metadata object only:',manifest['sha256'])
