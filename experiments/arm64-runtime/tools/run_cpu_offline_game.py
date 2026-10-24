#!/usr/bin/env python3
"""Bounded offline real-source translation. Output remains quarantined; generation never executes guest code."""
from pathlib import Path
import argparse,json,hashlib,resource,struct,subprocess,sys,time
from native_observer import NativeObserver
from cpu_code_map import parse
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('profile',type=Path);ap.add_argument('--blocks',type=int,default=32);ap.add_argument('--envelope',action='store_true');ap.add_argument('--selection',choices=['address','first-observed'],default='address');a=ap.parse_args();assert 1<=a.blocks<=256
 profile=a.profile.resolve();assert profile.parent==P/'evidence'
 captured=json.loads((profile/'result.json').read_text());case=(P/captured['case']).resolve();assert case.parent==P/'evidence'
 runtime=json.loads((case/'result.json').read_text());state=Path(runtime['state']).resolve();assert state.parent==P/'state'
 manifest=json.loads((state/'cpu-code-manifest.json').read_text());maps=[parse(f.read_bytes(),manifest,preserve_order=a.selection=='first-observed')for f in sorted(state.glob('cpu-code-*.map'))];assert maps
 if a.selection=='first-observed':
  assert len(maps)==1,'cross-file observation order is not established'
  entries=sorted(maps[0]['observed_unique_order'][:a.blocks])
 else:entries=sorted(set(x for row in maps for x in row['blocks']))[:a.blocks]
 assert entries
 boot=manifest['source'].encode();patch=str(case/'startup.bin').encode()
 metadata=None
 if a.envelope:
  assert captured['passed'],'envelope generation requires a completed real-game profile'
  metadata=json.loads((state/'cpu-cache-environment.json').read_text());assert metadata['schema']==1 and metadata['software_tso']and metadata['normal_jit']and metadata['initial_jit_bytes']in(16*1024*1024,128*1024*1024)
  assert metadata['patched_code']==manifest['identity_sha256']
  current=json.loads((P/'build/fex/generated/cpu_cache_build.json').read_text())
  assert metadata['runtime_build']==current['runtime_build']and metadata['bridge_build']==current['bridge_build'],'profile build is stale'

 def make(blocks=entries,identity=manifest['identity_sha256'],path=boot):
  extra=b''if not a.envelope else b''.join(bytes.fromhex(metadata[k])for k in ['runtime_build','bridge_build','bridge_abi','module_set','host_features','codegen_config'])
  return(b'BBAOT002'if a.envelope else b'BBAOT001')+struct.pack('<4I',len(manifest['sections']),len(blocks),len(path),len(patch))+bytes.fromhex(identity)+extra+path+patch+b''.join(struct.pack('<2Q',*s)for s in manifest['sections'])+b''.join(struct.pack('<Q',x)for x in blocks)
 e=P/'evidence'/f'cpu-offline-game-{time.time_ns()}';e.mkdir();binary=P/'build/fex/Bin/bb-cpu-cache-generator'
 env=dict(PATH='/usr/bin:/bin:/usr/sbin:/sbin',HOME=str(P/'state'),TMPDIR=str(P/'state')+'/',BB_AUDIO='none')
 result=dict(scope='quarantined real-source static CPU translation only; no game execution/loading or speed/memory benefit claim',profile=str(profile),profile_game_passed=captured['passed'],selection=a.selection,selected_entries=entries,selection_is_execution_heat=False,envelope=a.envelope,metadata=metadata,generator_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),rows=[],passed=False)
 n=NativeObserver();print('Evidence:',e,flush=True)
 def save():(e/'result.json').write_text(json.dumps(result,indent=2))
 def run(name,blob,expected):
  plan=e/(name+'.plan');output=e/(name+'.becc'if a.envelope else name+'-unvalidated.fxcc');plan.write_bytes(blob);before=n.system();assert not before['locked']and before['pressure']!=4
  start=time.monotonic();samples=[];stopped=None
  with(e/(name+'.log')).open('w')as log:
   child=subprocess.Popen([str(binary),str(plan),str(output)],stdout=log,stderr=subprocess.STDOUT,cwd=P,env=env)
   try:
    while child.poll()is None:
     row=n.system();row['seconds']=time.monotonic()-start;samples.append(row)
     if row['locked']or row['pressure']==4 or row['swap_mib']-before['swap_mib']>1024 or row['seconds']>30:
      stopped='guard';child.terminate();break
     time.sleep(.1)
    try:child.wait(timeout=3)
    except subprocess.TimeoutExpired:child.kill();child.wait()
   finally:
    if child.poll()is None:child.kill();child.wait()
  result['rows'].append(dict(name=name,exit=child.returncode,expected=expected,stopped=stopped,samples=samples,output_bytes=output.stat().st_size if output.exists() else 0));save();print(name,child.returncode,flush=True)
  assert child.returncode==expected and stopped is None
  if expected:assert not output.exists(),'rejected plan published output'
  else:
   text=(e/(name+'.log')).read_text();assert'guest_executed=0 cache_loaded=0'in text
   result['candidate_sha256']=hashlib.sha256(output.read_bytes()).hexdigest();result['candidate']=str(output)
   if a.envelope:assert'envelope=1 payload_validated=1'in text
 try:
  run('valid',make(),0)
  wrong=bytearray(make());wrong[24]^=0x80;run('wrong-identity',wrong,1)
  run('outside-entry',make(entries[:-1]+[manifest['image_bytes']+4096]),1)
  run('truncated',make()[:-1],1)
  run('extra-tail',make()+b'X',1)
  excess=bytearray(make());struct.pack_into('<I',excess,12,257);run('excess-entry-count',excess,1)
  run('outside-source-path',make(path=b'/etc/hosts'),1)
  if a.envelope:
   wrong=bytearray(make());wrong[56]^=0x80;run('wrong-runtime-build',wrong,1)
   wrong=bytearray(make());wrong[88]^=0x80;run('wrong-bridge-build',wrong,1)
   wrong=bytearray(make());wrong[184]^=0x80;run('wrong-host',wrong,1)
   wrong=bytearray(make());wrong[216]^=0x80;run('wrong-config',wrong,1)
  result['peak_child_rss_bytes']=resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
  result['passed']=True;save()
 finally:n.close()
 print(json.dumps(dict(evidence=str(e),checks=len(result['rows']),passed=result['passed'],peak_child_rss_bytes=result['peak_child_rss_bytes']),indent=2))
if __name__=='__main__':main()
