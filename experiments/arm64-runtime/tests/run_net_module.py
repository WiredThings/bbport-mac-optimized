#!/usr/bin/env python3
"""Real ARM bbnet + FEX guest fixture, no account or server requests."""
from pathlib import Path
import subprocess,time,json,sys,argparse
from game_fixture import make
ap=argparse.ArgumentParser();ap.add_argument('--fast-hle-lookup',action='store_true');args=ap.parse_args()
p=Path(__file__).resolve().parents[1]
e=p/'evidence'/f'net-module-fex-{time.time_ns()}';e.mkdir();results=[]
for mode,expected in [('net-thd-push-alias',0),('net-thd-read-valid',0),('net-thd-read-bad',74),('net-thd-recv-bad',74),('net-thd-npid-bad',74),('net-thd-path-bad',74),('net-world-info',0),('net-bad-callback',74),('net-bad-output',74),('net-http-handle',0),('net-http-span-valid',0),('net-http-span-bad',74),('net-http-span-overflow',74)]:
 fixture=p/'fixtures'/(mode+'.bbprobe');fixture.write_bytes(make(mode));state=p/'state'/f'{mode}-{time.time_ns()}';gpu=p/'state'/f'{state.name}-gpu';gpu.mkdir()
 env=dict(PATH='/usr/bin:/bin:/usr/sbin:/sbin',HOME=str(p/'state'),BB_GPU_USER_DIR=str(gpu),BB_USER_DIR=str(state),BB_ONLINE='1',BB_ARM_ALLOW_NETWORK='1',BB_NET_BACKEND='thd',BB_NET_MODULE=str(p/'build/bbnet-arm64/libbbnet.dylib'),BB_AUDIO='none')
 if __import__('os').environ.get('BB_ARM_PACKED_VENEERS')=='1':env['BB_ARM_PACKED_VENEERS']='1'
 if args.fast_hle_lookup:env['BB_ARM_HLE_FAST']='1'
 with (e/(mode+'.log')).open('w') as log:
  r=subprocess.run([p/'build/fex/Bin/bb-game-arm64','--authored-fixture','--boot',fixture,'--state',state],env=env,stdout=log,stderr=log,timeout=15)
 result=dict(test=mode,exit=r.returncode,expected=expected);results.append(result);(e/'results.json').write_text(json.dumps(results,indent=2)+'\n');assert r.returncode==expected,(e,result)
print(json.dumps(dict(evidence=str(e),tests=len(results),passed=True)))
