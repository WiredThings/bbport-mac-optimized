#!/usr/bin/env python3
"""Guarded Shared QA of the unchanged launcher shell pipeline with native components.
No frontend policy implementation; all patch/mod/network/restart preparation stays upstream.
"""
from pathlib import Path
import sys,os,time,json,secrets,shutil,subprocess,signal,argparse,hashlib
from native_observer import NativeObserver
from qa_memory_policy import MemoryPolicy
from launcher_environment import load as metal_environment
from launcher_language_policy import load as language_environment
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--online',action='store_true');ap.add_argument('--seconds',type=int,default=360);a=ap.parse_args();assert 0<a.seconds<=600
 obs=NativeObserver();before=obs.system();assert not before['locked']and before['pressure']!=4
 running=subprocess.check_output(['ps','-axo','pid,comm'],text=True);assert not any(l.rstrip().endswith(('/bb-probe','/bb-probe-metal','/bb-game-arm64','/bb-gpu'))for l in running.splitlines())
 stage=P/'launcher-component-stage';source_manifest=json.loads((stage/'SOURCE-PARITY.json').read_text());assert all(hashlib.sha256((stage/f).read_bytes()).hexdigest()==h for f,h in source_manifest['original_sources'].items())
 label=str(time.time_ns());e=P/'evidence'/('launcher-component-run-'+label);e.mkdir();user=P/'state'/('launcher-user-'+label);data=P/'state'/('launcher-data-'+label);user.mkdir();data.mkdir();out=data/'out';out.mkdir();pad=data/'pad.txt';pad.write_text('');nonce=secrets.token_hex(32)[:32]
 source=P/'state/qa-launcher-community-seed';shutil.copytree(source/'savedata',user/'savedata');shutil.copy2(source/'bbport.ini',user/'bbport.ini')
 adapted=[]
 for f in list((user/'savedata/1').iterdir()):
  if f.name in('CUSA03173','CUSA03173.memory'):
   target=f.with_name('CUSA00207'+f.name[9:]);assert not target.exists();f.rename(target);adapted.append((f.name,target.name))
 game=P.parent/'assets/CUSA03173-1.09';identity='ARMQA1\n'+str(out/'boot-linked.bin')+'\n'+str(game)+'\n'+str(out/'content.bin')+'\n'+nonce+'\n';(user/'.arm-runtime-session').write_text(identity)
 env=dict(PATH='/usr/bin:/bin:/usr/sbin:/sbin',HOME=str(P/'state'),TMPDIR=str(data)+'/',BB_GAME_DIR=str(game),BB_USER_DIR=str(user),BB_DATA_DIR=str(data),BB_CONFIG=str(user/'bbport.ini'),BB_GPU_USER_DIR=str(P/'state/gpu-stutter-fresh-1792770276384734000'),BB_ARM_LAUNCHER_COMPAT='1',BB_ARM_SESSION_NONCE=nonce,BB_ARM_RESTART_NONCE=secrets.token_hex(16),BB_ARM_QA_PAD_FILE=str(pad),BB_QUICKSAVE='1',BB_ONLINE='0',BB_SKIP_NETWORK_CHOICE='1',BB_AUDIO='none',BB_FPS='uncap',BB_MODS_ENABLED='1',BB_ARM_PACKED_VENEERS='1',BB_ARM_RESOURCE_HOOKS='1',BB_GUEST_HOOKS='1',BB_ARM_RESOURCE_FAST='1',BB_ARM_RESOURCE_LEAF='1',BB_ARM_EXTRA_HEAPS='1',BB_MTL_HOT_STAGING_REUSE='1',BB_METAL_ARM64='1')
 env.update(metal_environment());env.update(language_environment(P/'fixtures/current-launcher-language.json'))
 if a.online:
  from qa_online_policy import load_private_environment
  env.update(load_private_environment()) # Original run_optimized.sh generates the routes itself.
 cmd=['/bin/bash',str(stage/'tools/macos/run_optimized.sh')]
 (e/'conditions.json').write_text(json.dumps(dict(command=cmd,private_user=str(user),private_data=str(data),pad=str(pad),normal_save_seed=str(source),namespace_adapted_only_in_new_QA=adapted,online=a.online,script_parity=source_manifest,credentials_logged=False,silent=True,FPS_claim=False,formal_data_modified=False,memory_policy=MemoryPolicy.MANIFEST),indent=2)+'\n')
 print('Evidence:',e,flush=True);policy=MemoryPolicy(before);start=time.monotonic();samples=[];tracked={};stop=None;proc=None
 def rows():
  pending=[proc.pid];seen=set();allrows=[]
  while pending:
   parent=pending.pop()
   if parent in seen:continue
   seen.add(parent);assert len(seen)<=4096
   for pid,ppid,path in obs.processes(parent):
    allrows.append((pid,ppid,path));tracked[pid]=path
    if pid!=parent and pid not in seen:pending.append(pid)
  return list({pid:(pid,ppid,path)for pid,ppid,path in allrows}.values())
 def stop_owned():
  if proc.poll()is None:proc.terminate()
  try:proc.wait(timeout=3)
  except subprocess.TimeoutExpired:pass
  # Only descendants already observed under our own launched pipeline. Never name-match kill.
  for sig in (signal.SIGTERM,signal.SIGKILL):
   for pid,path in reversed(list(tracked.items())):
    try:
     if obs.path(pid)==path:os.kill(pid,sig)
    except ProcessLookupError:pass
   if sig==signal.SIGTERM:time.sleep(.3)
  if proc.poll()is None:proc.kill();proc.wait()
 try:
  with(e/'runtime.log').open('w')as log:
   proc=subprocess.Popen(cmd,cwd=stage,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);(e/'owner.json').write_text(json.dumps({'pid':proc.pid}))
   try:
    while proc.poll()is None:
     s=obs.system();s.update(seconds=time.monotonic()-start,processes=rows());samples.append(s)
     finish=data/'.private-test-finished'
     if finish.is_file()and not finish.is_symlink()and finish.read_text()==nonce:stop='private test completed'
     memory_stop=policy.observe(s['seconds'],s)
     if memory_stop:stop=memory_stop
     if s['seconds']>a.seconds:stop='time budget'
     if stop:stop_owned();break
     time.sleep(.2)
   finally:
    if proc.poll()is None:stop_owned()
   result=dict(exit=proc.returncode,stopped=stop,seconds=time.monotonic()-start,online=a.online,state=str(user),data=str(data),before=before,after=obs.system(),memory_policy=MemoryPolicy.MANIFEST,samples=samples,script_parity=True,formal_data_modified=False)
   (e/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items()if k!='samples'},indent=2),flush=True)
 finally:obs.close()
if __name__=='__main__':main()
