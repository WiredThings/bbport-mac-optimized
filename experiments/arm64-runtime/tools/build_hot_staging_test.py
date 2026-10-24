#!/usr/bin/env python3
from pathlib import Path
import shlex,subprocess,json
P=Path(__file__).resolve().parents[1];B=Path('/private/tmp/blood-echoes-arm64-metal-20261023/build');M=P/'vendor/metal_api';old='/Users/k.d./Documents/Codex/2026-10-16/https-github-com-loreanxavier-pt-pc/work/bbport-mac-optimized/engines/metal/gpu'
commands=subprocess.check_output(['ninja','-C',str(B),'-t','commands','texture-cache-views-test'],text=True).splitlines()
compile=next(shlex.split(l)for l in commands if ' -c 'in l and l.endswith('/test_texture_cache_views.mm'));compile=[x.replace(old,str(M))for x in compile if x not in ('-DNDEBUG','-DNDEBUG=1')];compile[1:1]=['-UNDEBUG','-I'+str(P/'tests/metal')]
obj=P/'build/test-hot-staging.o';compile[compile.index('-o')+1]=str(obj);compile[compile.index('-MF')+1]=str(obj.with_suffix('.d'));compile[-1]=str(P/'tests/metal/test_hot_staging.mm');subprocess.run(compile,cwd=B,check=True)
link=shlex.split(commands[-1].split('&&')[1].strip());dest=P/'build/test-hot-staging-metal';lib=P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.hot-staging-candidate.dylib'
for i,x in enumerate(link):
 if x.endswith('/test_texture_cache_views.mm.o'):link[i]=str(obj)
 elif x=='texture-cache-views-test':link[i]=str(dest)
 elif x=='libbbgpu.dylib':link[i]=str(lib)
 elif x.startswith('-Wl,-rpath,')and x.endswith('/build'):link[i]='-Wl,-rpath,'+str(lib.parent)
 elif not x.startswith('-')and(B/x).is_file():link[i]=str(B/x)
subprocess.run(link,cwd=B,check=True)
def deps(path):return [l.strip().split(' (compatibility')[0]for l in subprocess.check_output(['otool','-L',str(path)],text=True).splitlines()[1:]]
expected={Path(x).name:str((lib.parent/x.removeprefix('@loader_path/')).resolve())if x.startswith('@loader_path/')else x for x in deps(lib)}
for x in deps(dest):
 replacement=expected.get(Path(x).name)
 if replacement and replacement!=x:subprocess.run(['install_name_tool','-change',x,replacement,str(dest)],check=True)
assert str(lib)in deps(dest),'test not linked to private candidate'
subprocess.run(['codesign','--force','--sign','-',str(dest)],check=True);(P/'evidence/hot-staging-native-test-build.json').write_text(json.dumps(dict(compile=compile,link=link),indent=2)+'\n');print(dest)
