#!/usr/bin/env python3
"""Muted real-game ABI3 module + F3-to-offline regression, no account/server login."""
from pathlib import Path
import json,subprocess,sys,time,os
p=Path(__file__).resolve().parents[1];pad=p/'state'/f'net-f3-pad-{time.time_ns()}.txt';pad.write_text('')
old=set((p/'evidence').glob('game-*'))
cmd=[sys.executable,str(p/'tools/run_game.py'),'--net-module-smoke','--boot',str(p.parent/'assets/boot-linked.bin'),'--app0',str(p.parent/'assets/CUSA03173-1.09'),'--content-profile',str(p.parent/'assets/content.bin'),'--seed-user',str(p.parent/'qa-seed-user'),'--clone-private-session',str(p/'state/game-1792762608095377000'),'--gpu-state',str(p/'state/gpu-1792757530237447000'),'--graphics','--quicksave','--pad-file',str(pad),'--seconds','90']
use_fast='--legacy-hle-lookup' not in sys.argv
cmd+=['--fast-hle-lookup'] if use_fast else ['--legacy-hle-lookup']
if '--packed-veneers' in sys.argv:cmd+=['--packed-veneers']
if '--hot-staging-reuse' in sys.argv:cmd+=['--hot-staging-reuse']
if '--extra-heaps' in sys.argv:cmd+=['--extra-heaps']
if '--native-observer' in sys.argv:cmd+=['--native-observer']
if '--resource-hooks' in sys.argv:cmd+=['--resource-hooks']
if '--resource-fast-dispatch' in sys.argv:cmd+=['--resource-fast-dispatch']
if '--resource-leaf-dispatch' in sys.argv:cmd+=['--resource-leaf-dispatch']
console=p/'evidence'/f'net-f3-smoke-{time.time_ns()}.log'
with console.open('w')as log:
 proc=subprocess.Popen(cmd,stdout=log,stderr=log,cwd=p);e=None;ready=None;sent=False;cleared=False;second=False
 while proc.poll() is None:
  if e is None:
   fresh=[x for x in (p/'evidence').glob('game-*') if x not in old and (x/'runtime.log').exists()]
   if len(fresh)==1:e=fresh[0];print('Evidence:',e,flush=True)
  if e:
   text=(e/'runtime.log').read_text(errors='replace');now=time.monotonic()
   count=text.count('character ready; automatic menu input released')
   if count>=1 and ready is None:ready=now
   if ready and not sent and now-ready>=10:
    temp=pad.with_suffix('.tmp');temp.write_text('quickload\n');os.replace(temp,pad);sent=now
   if sent and not cleared and now-sent>=.35:
    temp=pad.with_suffix('.tmp');temp.write_text('');os.replace(temp,pad);cleared=True
   if count>=2:second=True
  time.sleep(.1)
 if proc.returncode:raise RuntimeError(f'supervisor failed: {console}')
 if not e:raise RuntimeError('no game evidence; preflight may have refused startup')
 r=json.loads((e/'result.json').read_text());text=(e/'runtime.log').read_text();report=dict(hot_staging_reuse='--hot-staging-reuse' in sys.argv,hot_staging_markers=text.count('Metal staging recent reuse: enabled;'),evidence=str(e),exit=r['exit'],stopped=r['stopped'],generations=r['generations'],second_character_ready=second,module_initializations=text.count('ARM NET private ARM64 module and typed guest bridge ready'),muted_streams=text.count('benchmark stream muted; original audio backend and cadence retained'),fast_hle_lookup=use_fast,packed_veneers='--packed-veneers' in sys.argv,resource_hooks='--resource-hooks' in sys.argv,resource_fast_dispatch='--resource-fast-dispatch' in sys.argv,resource_leaf_dispatch='--resource-leaf-dispatch' in sys.argv,resource_installs=text.count('ARM RESOURCE 10/10 startup FEX detours'),resource_metadata_rejections=text.count('ARM RESOURCE invalid metadata'),scope='real module + F3 to offline, no account/server login')
 (e/'network-f3-result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));assert r['exit']==0 and r['stopped']=='time budget' and second and report['module_initializations']==1 and report['muted_streams']>=4


if '--resource-hooks' in sys.argv:
 assert report['resource_installs']==2 and report['resource_metadata_rejections']==0
 assert text.count('Guest hooks: FEX startup detours ready, remote lifetime callback=1')==2
if '--resource-fast-dispatch' in sys.argv:
 assert text.count('ARM RESOURCE dispatch-filter=1')==2

if '--resource-leaf-dispatch' in sys.argv:
 assert text.count('ARM RESOURCE leaf-dispatch=1')==2

if "--extra-heaps" in sys.argv:assert text.count("ARM EXTRA 5/5 actual heap hooks installed")==2

if '--packed-veneers' in sys.argv:assert text.count('ARM VENEER layout=darwin-packed-guest-ro-v1 ')==2

if '--hot-staging-reuse' in sys.argv:assert report['hot_staging_markers']>=2
