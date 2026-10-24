#!/usr/bin/env python3
from pathlib import Path
import shlex,subprocess,json,os
P=Path(__file__).resolve().parents[1];B=Path('/private/tmp/blood-echoes-arm64-metal-20261023/build');M=P/'vendor/metal_api'
old='/Users/k.d./Documents/Codex/2026-10-16/https-github-com-loreanxavier-pt-pc/work/bbport-mac-optimized/engines/metal/gpu'
commands=subprocess.check_output(['ninja','-C',str(B),'-t','commands','texture-cache-views-test'],text=True).splitlines()
compile=next(shlex.split(l) for l in commands if ' -c ' in l and l.endswith('/test_texture_cache_views.mm'))
compile=[x.replace(old,str(M)) for x in compile];obj=P/'build/texture-expansion-test.o'
compile[compile.index('-o')+1]=str(obj);compile[compile.index('-MF')+1]=str(obj.with_suffix('.d'));compile[-1]=str(P/'tests/metal/test_texture_expansion.mm')
subprocess.run(compile,cwd=B,check=True)
link=shlex.split(commands[-1].split('&&')[1].strip());dest=P/'build/texture-expansion-test'
for i,x in enumerate(link):
 if x.endswith('/test_texture_cache_views.mm.o'):link[i]=str(obj)
 elif x=='texture-cache-views-test':link[i]=str(dest)
 elif x=='libbbgpu.dylib':link[i]=str(P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib')
 elif x.startswith('-Wl,-rpath,') and x.endswith('/build'):link[i]='-Wl,-rpath,'+str(P/'backend/bbport/bin/gpu-metal-arm64')
 elif not x.startswith('-') and (B/x).is_file():link[i]=str(B/x)
subprocess.run(link,cwd=B,check=True)
def dylibs(path):return [l.strip().split(' (compatibility')[0] for l in subprocess.check_output(['otool','-L',str(path)],text=True).splitlines()[1:]]
lib=P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib'
expected={Path(x).name:(str((lib.parent/x.removeprefix('@loader_path/')).resolve()) if x.startswith('@loader_path/') else x) for x in dylibs(lib)}
for x in dylibs(dest):
 replacement=expected.get(Path(x).name)
 if replacement and replacement!=x:subprocess.run(['install_name_tool','-change',x,replacement,str(dest)],check=True)
subprocess.run(['codesign','--force','--sign','-',str(dest)],check=True)
(P/'evidence/texture-expansion-build.json').write_text(json.dumps(dict(compile=compile,link=link),indent=2))
print(dest)
