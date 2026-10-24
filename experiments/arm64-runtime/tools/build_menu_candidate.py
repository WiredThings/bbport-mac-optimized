from pathlib import Path
import json,shlex,subprocess,hashlib,time
P=Path('/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype');B=Path('/private/tmp/blood-echoes-arm64-metal-20261023/build');M=P/'vendor/metal_api'
source='shim/bbport_game_menu.cpp'
commands=subprocess.check_output(['ninja','-C',str(B),'-t','commands'],text=True).splitlines()
cmd=next(shlex.split(x)for x in commands if x.startswith(('/usr/bin/c++','/usr/bin/clang++'))and x.endswith('/'+source))
old='/Users/k.d./Documents/Codex/2026-10-16/https-github-com-loreanxavier-pt-pc/work/bbport-mac-optimized/engines/metal/gpu'
cmd=[x.replace(old,str(M))for x in cmd];obj=P/'build/native-bbport_game_menu.o';cmd[cmd.index('-o')+1]=str(obj);cmd[cmd.index('-MF')+1]=str(obj.with_suffix('.d'))
subprocess.run(cmd,cwd=B,check=True)
remote_source='shim/remote/remote_front.cpp'
remote_cmd=next(shlex.split(x)for x in commands if x.startswith(('/usr/bin/c++','/usr/bin/clang++'))and x.endswith('/'+remote_source))
remote_cmd=[x.replace(old,str(M))for x in remote_cmd];remote_obj=P/'build/native-remote_front-menu.o'
remote_cmd[1:1]=['-I'+str(P/'src')]
remote_cmd[remote_cmd.index('-o')+1]=str(remote_obj);remote_cmd[remote_cmd.index('-MF')+1]=str(remote_obj.with_suffix('.d'))
subprocess.run(remote_cmd,cwd=B,check=True)
base=json.loads((P/'evidence/heap-bridge-build.json').read_text());dest=P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib'
assert hashlib.file_digest(dest.open('rb'),'sha256').hexdigest()==base['sha256'],'freeze live baseline before reusing object closure'
link=list(base['link']);indices=[i for i,x in enumerate(link)if x.endswith('shim/bbport_game_menu.cpp.o')];assert len(indices)==1;link[indices[0]]=str(obj)
remote_indices=[i for i,x in enumerate(link)if x.endswith(('shim/remote/remote_front.cpp.o','native-remote_front.o'))];assert len(remote_indices)==1;link[remote_indices[0]]=str(remote_obj)
out=dest.with_name('libbbgpu.menu-candidate.dylib');link[link.index('-o')+1]=str(out);subprocess.run(link,cwd=B,check=True)
def deps(f):return[x.strip().split(' (compatibility')[0]for x in subprocess.check_output(['otool','-L',str(f)],text=True).splitlines()[1:]]
expected={Path(x).name:x for x in deps(dest)}
for x in deps(out):
 replacement=expected.get(Path(x).name)
 if replacement and replacement!=x:subprocess.run(['install_name_tool','-change',x,replacement,str(out)],check=True)
subprocess.run(['install_name_tool','-id',str(out),str(out)],check=True)
subprocess.run(['codesign','--force','--sign','-',str(out)],check=True)
(P/'evidence/menu-candidate-build.json').write_text(json.dumps(dict(compile=cmd,remote_compile=remote_cmd,exit_protocol_header_sha256=hashlib.sha256((P/'src/runtime_exit_codes.h').read_bytes()).hexdigest(),link=link,candidate=str(out),baseline=base['sha256'],sha256=hashlib.file_digest(out.open('rb'),'sha256').hexdigest()),indent=2)+'\n')
print('UNINSTALLED MENU CANDIDATE',out)
