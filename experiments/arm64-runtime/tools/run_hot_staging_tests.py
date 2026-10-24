#!/usr/bin/env python3
"""Headless real Metal candidate verification, private state and no game."""
from pathlib import Path
import hashlib,json,subprocess,time,sys
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'tools'));from run_game import system

def main():
 e=P/'evidence'/f'hot-staging-native-{time.time_ns()}';e.mkdir();r=dict(scope='headless real Metal, no game, independent uninstalled candidate; assertions enabled',runs=[],passed=False)
 lib=P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.hot-staging-candidate.dylib';frozen=hashlib.file_digest(lib.open('rb'),'sha256').hexdigest();r['candidate_sha256']=frozen
 print('Native evidence:',e,flush=True)
 for mode in ['0','1']:
  for debug in ['0','1']:
   status=system();assert not status['locked']and status['pressure']!=4
   state=P/'state'/f'hot-staging-native-{mode}-{debug}-{time.time_ns()}';state.mkdir(mode=0o700)
   env=dict(PATH='/usr/bin:/bin:/usr/sbin:/sbin',HOME=str(state),BB_USER_DIR=str(state),BB_GPU_USER_DIR=str(state),MTL_DEBUG_LAYER=debug,DYLD_PRINT_LIBRARIES='1')
   x=subprocess.run([str(P/'build/test-hot-staging-metal'),mode],env=env,capture_output=True,text=True,timeout=45)
   (e/f'{mode}-debug{debug}.log').write_text(x.stdout+x.stderr);row=dict(mode=mode,metal_debug_layer=debug,exit=x.returncode,candidate_loaded=str(lib)in x.stderr,summary=[s for s in x.stdout.splitlines()if 'HOT_STAGING_METAL'in s]);r['runs'].append(row);(e/'result.json').write_text(json.dumps(r,indent=2)+'\n');print(row,flush=True)
   assert row['candidate_loaded']and x.returncode==0 and 'PASS'in x.stdout,'real Metal validation failed; leave baseline installed'
   assert hashlib.file_digest(lib.open('rb'),'sha256').hexdigest()==frozen
 r['passed']=True;(e/'result.json').write_text(json.dumps(r,indent=2)+'\n')
if __name__=='__main__':main()
