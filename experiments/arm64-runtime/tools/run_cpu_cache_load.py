#!/usr/bin/env python3
"""Load exact quarantined game cache in new processes; never execute guest code."""
from pathlib import Path
import argparse
import hashlib,json,resource,struct,subprocess,time,sys
from native_observer import NativeObserver
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--map-jit',action='store_true');a=ap.parse_args()
 source=(P/'evidence/cpu-offline-game-1792804130117083000');original=(source/'valid-unvalidated.fxcc').read_bytes();plan=source/'valid.plan'
 e=P/'evidence'/f'cpu-cache-load-{time.time_ns()}';e.mkdir();n=NativeObserver()
 binary=P/'build/fex/Bin/bb-cpu-cache-generator'
 report=dict(scope='exact known candidate only, structural validation and relocation without executing guest; no runtime caching enabled',binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),candidate_sha256=hashlib.sha256(original).hexdigest(),map_jit_startup_only=a.map_jit,guest_execution_ready=False,rows=[],passed=False)
 env=dict(PATH='/usr/bin:/bin:/usr/sbin:/sbin',HOME=str(P/'state'),TMPDIR=str(P/'state')+'/',BB_AUDIO='none')
 print('Evidence:',e,flush=True)
 def save():(e/'result.json').write_text(json.dumps(report,indent=2))
 def case(name,data,expected,override=None):
  f=e/(name+'.fxcc');f.write_bytes(data);before=n.system();assert not before['locked'] and before['pressure']!=4
  start=time.monotonic();samples=[];stop=None
  with(e/(name+'.log')).open('w')as log:
   child=subprocess.Popen([str(binary),str(plan),str(f),'--validate-load-map-jit'if a.map_jit else'--validate-load',override or hashlib.sha256(data).hexdigest()],stdout=log,stderr=subprocess.STDOUT,env=env,cwd=P)
   try:
    while child.poll()is None:
     row=n.system();row['seconds']=time.monotonic()-start;samples.append(row)
     if row['locked']or row['pressure']==4 or row['swap_mib']-before['swap_mib']>1024 or row['seconds']>20:
      stop='CPU-only guard';child.terminate();break
     time.sleep(.05)
    try:child.wait(timeout=3)
    except subprocess.TimeoutExpired:child.kill();child.wait()
   finally:
    if child.poll()is None:child.kill();child.wait()
  text=(e/(name+'.log')).read_text(errors='replace');row=dict(name=name,exit=child.returncode,expected=expected,stopped=stop,samples=samples,log=text)
  report['rows'].append(row);save();print(name,child.returncode,flush=True)
  assert child.returncode==expected and stop is None
  if expected:assert'loaded_no_execute=1'not in text and'validated_before_FEX=1'not in text
  else:
   assert all(k in text for k in ('validated_before_FEX=1','loaded_no_execute=1','cleared=1 unregistered=1','guest_executed=0'))
   assert ('live_backpatch_safe=1 expected_protection_fault=0'if a.map_jit else'live_backpatch_safe=0 expected_protection_fault=1')in text
 def mutate(at,value,width=4):
  data=bytearray(original);data[at:at+width]=value.to_bytes(width,'little');return data
 try:
  case('valid-process-a',original,0);case('valid-process-b',original,0)
  case('wrong-sha',original,1,'0'*64)
  case('short-header',original[:55],1)
  case('truncated-tail',original[:-1],1)
  case('extra-tail',original+b'X',1)
  case('wrong-version',mutate(4,3),1)
  case('wrong-fex',mutate(8,1,1),1)
  case('excess-blocks',mutate(28,65537),1)
  case('excess-pages',mutate(32,32769),1)
  case('excess-code',mutate(36,32<<20),1)
  case('excess-relocs',mutate(40,131073),1)
  case('wrong-base',mutate(48,1,8),1)
  case('outside-guest',mutate(56,512<<20,8),1)
  case('unaligned-host',mutate(64,3,8),1)
  case('wrong-edge-count',mutate(72,0,8),1)
  case('unknown-guest-page',mutate(80,512<<20,8),1)
  # Locate the relocation list using the frozen serializer's checked widths.
  at=56
  for _ in range(struct.unpack_from('<I',original,28)[0]):at+=24+8*struct.unpack_from('<Q',original,at+16)[0]
  case('bad-reloc-kind',mutate(at+8,1),1)
  case('outside-reloc',mutate(at,32<<20,8),1)
  kind=struct.unpack_from('<I',original,at+8)[0]
  if kind==3:case('reserved-register',mutate(at+12,18,1),1)
  report['checks']=len(report['rows']);report['peak_child_rss_bytes']=resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
  report['passed']=True;save()
 finally:n.close()
 print(json.dumps(dict(evidence=str(e),passed=report['passed'],checks=len(report['rows']),peak_child_rss_bytes=report['peak_child_rss_bytes']),indent=2))
if __name__=='__main__':main()
