#!/usr/bin/env python3
"""Actual syscall/HLE bridge cache fixture. Never starts game or accesses real game data."""
from pathlib import Path
import hashlib,json,re,subprocess,time,argparse
P=Path(__file__).resolve().parents[1]
B=P/'build/fex/Bin/test_cpu_bridge_disk_cache'
OLD=P/'evidence/cache-serializer-red-1792802511887939000/test_cpu_bridge_disk_cache-original'
def sha(p):
 with p.open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--map-jit',action='store_true');ap.add_argument('--normal-consumer',action='store_true');ap.add_argument('--engine-consumer',action='store_true');ap.add_argument('--reserve-128',action='store_true');a=ap.parse_args();a.normal_consumer=a.normal_consumer or a.engine_consumer;a.map_jit=a.map_jit or a.normal_consumer
 B=P/'build/fex/Bin'/('test_cpu_bridge_engine_consumer'if a.engine_consumer else'test_cpu_bridge_normal_consumer'if a.normal_consumer else'test_cpu_bridge_map_jit'if a.map_jit else'test_cpu_bridge_disk_cache')
 expected_calls='seven_calls=1 pair_calls=1'
 e=P/'evidence'/f'cpu-bridge-cache-{time.time_ns()}';e.mkdir();cache=e/'bridge.becc'
 assert not a.reserve_128 or a.engine_consumer
 env=dict(PATH='/usr/bin:/bin:/usr/sbin:/sbin',HOME=str(P/'state'),TMPDIR=str(P/'state')+'/',BB_AUDIO='none')
 if a.reserve_128:env['BB_TEST_JIT_RESERVE_128']='1'
 protected=[P/'build/fex/Bin/bb-game-arm64',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib'];before={str(f):sha(f)for f in protected}
 result=dict(scope='authored x86 code, actual project syscall/HLE/typed ABI implementations; eager cache, no game or performance claim',reserve_128=a.reserve_128,engine_consumer=a.engine_consumer,normal_consumer=a.normal_consumer,map_jit=a.map_jit,fixture_sha256=sha(B),frozen_sha256=sha(OLD),rows=[],passed=False)
 def save():(e/'result.json').write_text(json.dumps(result,indent=2))
 def run(name,command,expected=0,marker=None,fast=False):
  assert sha(B)==result['fixture_sha256']and sha(OLD)==result['frozen_sha256']
  local=dict(env)
  if fast:local['BB_ARM_HLE_FAST']='1'
  start=time.monotonic()
  with(e/(name+'.log')).open('w')as log:r=subprocess.run(list(map(str,command)),stdout=log,stderr=subprocess.STDOUT,cwd=P,env=local,timeout=15)
  text=(e/(name+'.log')).read_text();result['rows'].append(dict(name=name,exit=r.returncode,expected=expected,seconds=time.monotonic()-start));save();print(name,r.returncode,flush=True)
  assert r.returncode==expected and(not marker or marker in text),(name,text)
  return text
 try:
  h=P/'vendor/aether/runtime/sources/fexcore-darwin/FEXCore/include/FEXCore/Core/HostFeatures.h'
  features=set(re.findall(r'\b(?:bool|uint32_t)\s+(\w+)\s*\{\}',h.read_text()))
  inputs=(P/'src/bb_cpu_cache_inputs.h').read_text();covered=set(re.findall(r'FIELD\((\w+)\)',inputs))-{'name'}
  assert features==covered,(features-covered,covered-features)
  result['host_feature_fields_covered']=len(features)
  text=run('identity-boundaries',[P/'build/fex/Bin/test_cpu_cache_identity'],marker='passed=1');result['identity_assertions']=int(re.search(r'checks=(\d+)',text)[1])
  run('original-readonly-red',[OLD,'generate-bad-fd',e/'original-readonly.fxcc','0x8000000000','none'],41,'readonly_save_reported_success=1')
  for mode in ['generate-bad-fd','generate-limited-fd','generate-offset-fd','generate-pipe-fd']:
   run('fixed-'+mode,[B,mode,e/(mode+'.fxcc'),'0x8000000000','none'],marker='save_reported_success=0')
  gen=run('generate',[B,'generate',cache,'0x8000000000','none'],marker='generated=1');digest=sha(cache)
  for mode in ['load','load-reordered','load-changed-patch','load-changed-config','load-wrong-runtime-key','load-hardware-key']:
   text=run(mode,[B,mode,cache,'0x9000000000',digest],0 if mode=='load'else 30,'no_fallback_jit=1'if mode=='load'else'identity_or_structure_rejected_before_FEX_load=1')
   if mode=='load':
    result['host_aslr_changed']=re.search(r'host_fn=(\S+)',gen)[1]!=re.search(r'host_fn=(\S+)',text)[1];assert result['host_aslr_changed']
    assert expected_calls in text
   else:assert'seven_calls=0 pair_calls=0'in text
  text=run('load-fast-hle',[B,'load',cache,'0xa000000000',digest],marker='no_fallback_jit=1',fast=True)
  assert expected_calls in text
  if a.map_jit and not a.engine_consumer:
   assert all('cache_republished=1 changed_target_recompiled=1 second_clear=1'in(e/(name+'.log')).read_text()for name in ['load','load-fast-hle'])
   assert all('linked_new_target=1 cache_source_patched=1 stable_second_dispatch=1 cleared_links=1'in(e/(name+'.log')).read_text()for name in ['load','load-fast-hle'])
   assert all('pointer_calls=1 unaligned_dynamic_load_correct=1'in(e/(name+'.log')).read_text()for name in ['load','load-fast-hle'])
   assert all('BACHATA_UNALIGNED_FIX'in(e/(name+'.log')).read_text()for name in ['load','load-fast-hle'])
  if a.engine_consumer:
   for name in ['load','load-fast-hle']:
    text=(e/(name+'.log')).read_text()
    assert'BACHATA_UNALIGNED_FIX'in text and'bounded_rejects=10 invalidation_new_target=1 busy_inspect_rejected=1 linked_reuse=1 released=1 normal_jit=1'in text
  if a.normal_consumer:
   assert all(('initial_jit_bytes=134217728 consumer=1'if a.reserve_128 else'initial_jit_bytes=16777216 consumer=1')in(e/(name+'.log')).read_text()for name in ['load','load-fast-hle'])
  result['cache_sha256']=digest;result['passed']=True
 finally:
  result['game_artifacts_unchanged']=before=={str(f):sha(f)for f in protected};result['protected_artifact_sha256']=before;save()
 assert result['game_artifacts_unchanged']
 print(json.dumps(dict(evidence=str(e),process_checks=len(result['rows']),identity_assertions=result['identity_assertions'],passed=result['passed'],game_artifacts_unchanged=True),indent=2))
if __name__=='__main__':main()
