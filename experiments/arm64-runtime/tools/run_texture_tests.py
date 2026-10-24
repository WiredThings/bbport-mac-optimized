#!/usr/bin/env python3
"""Small real Metal readback red/green tests; no game resources or production paths."""
from pathlib import Path
import subprocess,os,json,time,hashlib,shutil
P=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.file_digest(p.open('rb'),'sha256').hexdigest()
def main():
 e=P/'evidence'/('texture-suite-'+str(time.time_ns()));e.mkdir()
 state=P/'state'/('texture-suite-'+str(time.time_ns()));state.mkdir()
 baseline_dir=P/'build/texture-baseline';baseline_dir.mkdir(exist_ok=True)
 baseline=baseline_dir/'libbbgpu.dylib';shutil.copy2(P/'build/libbbgpu.before-5444.dylib',baseline)
 deps=P/'backend/bbport/deps-metal-arm64/lib'
 for l in subprocess.check_output(['otool','-L',str(baseline)],text=True).splitlines()[1:]:
  path=l.strip().split(' (compatibility')[0]
  if path.startswith('@loader_path/'):
   local=deps/Path(path).name;assert local.is_file();subprocess.run(['install_name_tool','-change',path,str(local),str(baseline)],check=True)
 subprocess.run(['codesign','--force','--sign','-',str(baseline)],check=True)
 results=[]
 for old in [True,False]:
  env=dict(PATH='/usr/bin:/bin:/usr/sbin:/sbin',HOME=str(state),BB_USER_DIR=str(state),BB_GPU_USER_DIR=str(state),DYLD_PRINT_LIBRARIES='1')
  if old:env['DYLD_LIBRARY_PATH']=str(baseline_dir)
  for case in ['cpu-dirty','cpu-clean','gpu-valid','gpu-cpu-dirty']:
   expected=7 if old and case in ['cpu-dirty','gpu-cpu-dirty'] else 0
   r=subprocess.run([str(P/'build/texture-expansion-test'),case],env=env,capture_output=True,text=True,timeout=20)
   loaded=str(baseline) if old else str(P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib')
   assert loaded in r.stderr,'test did not load intended library'
   name=('original-' if old else 'fixed-')+case;(e/(name+'.log')).write_text(r.stdout+r.stderr)
   results.append(dict(test=name,exit=r.returncode,expected=expected,library=loaded,sha256=sha(Path(loaded))))
   assert r.returncode==expected,(name,r.returncode,r.stderr[-2000:])
 (e/'results.json').write_text(json.dumps(results,indent=2));print(json.dumps(dict(evidence=str(e),tests=len(results),passed=True)))
if __name__=='__main__':main()
