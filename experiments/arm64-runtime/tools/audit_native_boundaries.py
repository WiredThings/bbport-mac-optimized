#!/usr/bin/env python3
"""Read-only architecture/dependency and source-boundary inventory, not proof of ABI safety."""
from pathlib import Path
import hashlib,json,re,shlex,subprocess,time
P=Path(__file__).resolve().parents[1]
def output(args):return subprocess.check_output(args,text=True,stderr=subprocess.STDOUT)
def main():
 e=P/'evidence'/f'x86-boundary-audit-{time.time_ns()}';e.mkdir()
 roots=[P/'build/fex/Bin/bb-game-arm64',P/'backend/bbport/bin/Bloodborne Metal.app/Contents/MacOS/bb-gpu',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib',P/'build/bbnet-arm64/libbbnet.dylib',P/'build/qa_online_account']
 gui=P/'build/launcher-gui/out/Products/Release/BBLauncher'
 if gui.is_file():roots.append(gui)
 # Include the unchanged launch pipeline's packaged ARM64 Python and all its
 # loadable native standard-library modules, not just the game/renderer hosts.
 python=P/'launcher-component-stage/python'
 if python.is_dir():roots += [python/'bin/python3']+sorted((python/'lib/python3.11/lib-dynload').glob('*.so'))
 binaries=[];seen=set();pending=[(f,[])for f in roots];unresolved=[]
 while pending:
  path,inherited=pending.pop(0);path=path.resolve()
  if str(path)in seen:continue
  seen.add(str(path));row=dict(path=str(path),exists=path.is_file());binaries.append(row)
  if not row['exists']:continue
  row['archs']=output(['/usr/bin/lipo','-archs',str(path)]).strip().split();row['has_arm64']='arm64'in row['archs'];row['sha256']=hashlib.file_digest(path.open('rb'),'sha256').hexdigest()
  raw=output(['/usr/bin/otool','-l',str(path)]);rpaths=[]
  for match in re.finditer(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset',raw):
   val=match[1].replace('@loader_path',str(path.parent));rpaths.append(val)
  row['rpaths']=rpaths;deps=[]
  ids=output(['/usr/bin/otool','-D',str(path)]).splitlines()[1:];install_ids={v.strip()for v in ids}
  for line in output(['/usr/bin/otool','-L',str(path)]).splitlines()[1:]:
   ref=line.strip().split(' (compatibility')[0];dep=dict(reference=ref)
   if ref in install_ids:dep['resolution']='own install name, not a dependency';deps.append(dep);continue
   if ref.startswith(('/usr/lib/','/System/Library/')):dep['resolution']='system dyld cache / system architecture slice';deps.append(dep);continue
   candidates=[]
   if ref.startswith('@loader_path'):candidates=[Path(ref.replace('@loader_path',str(path.parent)))]
   elif ref.startswith('@rpath/'):
    candidates=[Path(v)/ref[7:]for v in rpaths+inherited if '@'not in v]
   elif ref.startswith('/'):candidates=[Path(ref)]
   found=next((c.resolve()for c in candidates if c.is_file()),None)
   if found:
    dep.update(resolution='file',path=str(found))
    if found!=path:pending.append((found,rpaths+inherited))
   else:dep['resolution']='unresolved';unresolved.append(dict(image=str(path),reference=ref,candidates=[str(x)for x in candidates]))
   deps.append(dep)
  row['dependencies']=deps
 sources={}
 for build,target in [(P/'build/fex','bb-game-arm64'),(P/'build/bbnet-arm64','bbnet')]:
  commands=output(['/opt/homebrew/bin/ninja','-C',str(build),'-t','commands',target]).splitlines()
  for command in commands:
   if ' -c 'not in command:continue
   args=shlex.split(command);source=Path(args[-1])
   if not source.is_absolute():source=build/source
   if source.is_file()and source.is_relative_to(P):sources[str(source.resolve())]=dict(build=str(build),target=target,source=str(source.resolve()))
 groups={
 'host_x86_assembly_intrinsics':r'__asm|\basm\s*\(|\b_mm_\w+\s*\(|\b(?:_rdtsc|__rdtsc|_xgetbv|__cpuid)\s*\(|xmmintrin|emmintrin|immintrin|\b[fg]s:\[',
 'architecture_guards':r'__x86_64__|__aarch64__|sysv_abi|ms_abi',
 'dynamic_module_boundary':r'\bdlopen\s*\(|\bdlsym\s*\(|guest_call|CallGuest|CallWords|bb_arm_call',
 'possible_direct_callback':r'(?:->|\.)\s*(?:[A-Za-z_]*(?:callback|Callback|cbFunc|cb_func|handler|Handler|entry|func|destructor)[A-Za-z_]*)\s*\(|reinterpret_cast<[^\n]*(?:Cb|Callback|callback|\(\*\))[^\n]*\)\s*\(',
 'plain_callback_calls':r'\b(?:[A-Za-z_]*(?:[Cc]allback|[Hh]andler|cbFunc|cb_func)|func)\s*\(',
 'opaque_and_guest_pointer':r'void\s*\*\s*(?:pUserArg|user_arg|userdata)|guest_alloc|LowAllocator|GuestSnapshot',
 }
 # Include the actual generated runtime's literal .c includes and local headers.
 # This conservative candidate-source closure can include conditional branches;
 # it is intentionally not advertised as preprocessor/AST proof.
 closure=set(sources);pending_sources=list(sources)
 include_roots=[P/'src',P/'generated',P/'vendor/bbnet',P/'vendor/bbnet/include',P/'vendor/aether/src',P/'vendor/bb_runtime/src',P/'vendor/metal_api/shim',P/'vendor/metal_api/shadps4']
 while pending_sources:
  source=Path(pending_sources.pop())
  if '/fexcore-darwin/'in str(source)or '/External/'in str(source):continue
  for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"',source.read_text(errors='replace'),re.M):
   candidates=[Path(name)]if name.startswith('/')else[source.parent/name]+[base/name for base in include_roots]
   found=next((f.resolve()for f in candidates if f.is_file()and f.resolve().is_relative_to(P)),None)
   if found and str(found)not in closure:closure.add(str(found));pending_sources.append(str(found))
 hits={k:[]for k in groups}
 for source in closure:
  f=Path(source)
  if '/fexcore-darwin/'in source or '/External/'in source:continue
  for n,line in enumerate(f.read_text(errors='replace').splitlines(),1):
   for name,pattern in groups.items():
    if re.search(pattern,line):hits[name].append(dict(path=str(f.relative_to(P)),line=n,text=line.strip()[:450]))
 (e/'binary-graph.json').write_text(json.dumps(dict(roots=[str(x)for x in roots],images=binaries,unresolved=unresolved,complete=not unresolved,scope='explicit candidate images and recursive file dependencies; system-cache slices not enumerated; runtime dlopen policy requires separate review'),indent=2)+'\n')
 (e/'candidate-include-closure.json').write_text(json.dumps(sorted(closure),indent=2)+'\n')
 (e/'active-sources.json').write_text(json.dumps(list(sources.values()),indent=2)+'\n')
 (e/'source-hits.json').write_text(json.dumps(hits,indent=2)+'\n')
 print('Evidence:',e)
 print(json.dumps(dict(images=len(binaries),non_arm64=[r['path']for r in binaries if r.get('exists')and not r.get('has_arm64')],unresolved=unresolved,compiled_sources=len(sources),candidate_include_files=len(closure),source_hits={k:len(v)for k,v in hits.items()}),indent=2))
if __name__=='__main__':main()
