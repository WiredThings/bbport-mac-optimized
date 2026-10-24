#!/usr/bin/env python3
"""FEX resource detour regressions, authored fixtures only, no game window."""
from pathlib import Path
import json,os,subprocess,time,sys
from resource_fixture import make
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from analyze_stutter import read
P=Path(__file__).resolve().parents[1]
e=P/'evidence'/f'resource-detours-{time.time_ns()}';e.mkdir()
result=[]
gpu=P/'state'/f'resource-fixture-gpu-{time.time_ns()}';gpu.mkdir()
for name,kwargs,enabled,expected in [('valid',{},True,0),('bad-metadata',{'bad_metadata':True},True,0),('corrupt',{'corrupt':True},True,1),('original-no-coverage',{},False,2)]:
 boot=P/'fixtures'/f'resource-hooks-{name}.bbprobe';boot.write_bytes(make(**kwargs))
 env=dict(BB_GPU_USER_DIR=str(gpu),PATH='/usr/bin:/bin:/usr/sbin:/sbin',HOME=str(P/'state'),TMPDIR=str(P/'state')+'/',BB_ONLINE='0',BB_QUICKSAVE='0',BB_UPSCALER='off',BB_AUDIO='none',BB_GPU_LIFETIME='1',BB_ARM_HLE_FAST='0' if '--legacy-hle-lookup' in sys.argv else '1')
 if __import__('os').environ.get('BB_ARM_PACKED_VENEERS')=='1':env['BB_ARM_PACKED_VENEERS']='1'
 if enabled:env['BB_ARM_RESOURCE_HOOKS']='1'
 if '--fast-dispatch' in sys.argv:env['BB_ARM_RESOURCE_FAST']='1'
 if '--leaf-dispatch' in sys.argv:env.update(BB_ARM_RESOURCE_FAST='1',BB_ARM_RESOURCE_LEAF='1')
 if '--audit' in sys.argv:env['BB_STUTTER_AUDIT']=str(e/f'{name}-audit')
 started=time.monotonic()
 with(e/f'{name}.log').open('w')as log:r=subprocess.run([P/'build/fex/Bin/bb-game-arm64','--authored-fixture','--boot',boot,'--state',P/'state'/f'resource-{name}-{time.time_ns()}'],stdout=log,stderr=log,env=env,cwd=P,timeout=20)
 text=(e/f'{name}.log').read_text(errors='replace')
 result.append(dict(test=name,exit=r.returncode,expected=expected,seconds=time.monotonic()-started))
 (e/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 print(result[-1],flush=True)
 assert r.returncode==expected,f'{name}: {e}'
 if name=='valid' and '--audit' in sys.argv:
  traces=[read(p)for p in e.glob('valid-audit-*.bin')];rs=[r for trace in traces for r in trace['rows']if r['kind']==33]
  assert traces and all(t['dropped']==0 and t['overwritten']==0 for t in traces)
  assert {r['arg']&255 for r in rs}==set(range(7)) and all(r['wall']>=0 and r['cpu']>=0 for r in rs)
  print('all 7 resource timing kinds covered; bounded trace complete')
 if name=='valid':assert '10/10 startup FEX detours' in text and f"ARM RESOURCE fixture kind=6 calls={1 if '--fast-dispatch' in sys.argv or '--leaf-dispatch' in sys.argv else 3}" in text
 if name=='bad-metadata':assert 'ARM RESOURCE invalid metadata; lifetime reclamation disabled' in text
 if name=='corrupt':assert 'hook original mismatch' in text and '10/10 startup FEX detours' not in text
print('Evidence:',e)
