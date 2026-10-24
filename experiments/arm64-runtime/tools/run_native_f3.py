#!/usr/bin/env python3
"""Silent, file-input F3 regression using only a fresh cloned QA checkpoint."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,time
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--packed-veneers',action='store_true');ap.add_argument('--cpu-cache',type=Path);ap.add_argument('--jit-reserve-mib',type=int,choices=[16,128],default=16);a=ap.parse_args()
 if a.cpu_cache:
  a.cpu_cache=a.cpu_cache.resolve();assert a.cpu_cache.is_file()and a.cpu_cache.is_relative_to(P/'evidence')
 e=P/'evidence'/f'native-f3-{time.time_ns()}';e.mkdir();pad=P/'state'/f'f3-pad-{time.time_ns()}.txt';pad.write_text('')
 old=set((P/'evidence').glob('game-*'));binary=P/'build/fex/Bin/bb-game-arm64'
 cmd=[sys.executable,str(P/'tools/run_game.py'),'--boot',str(P.parent/'assets/boot-linked.bin'),'--app0',str(P.parent/'assets/CUSA03173-1.09'),'--content-profile',str(P.parent/'assets/content.bin'),'--seed-user',str(P.parent/'qa-seed-user'),'--clone-private-session',str(P/'state/game-1792762608095377000'),'--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--graphics','--quicksave','--pad-file',str(pad),'--seconds','100','--native-observer','--resource-hooks','--resource-fast-dispatch','--resource-leaf-dispatch','--extra-heaps']
 if a.packed_veneers:cmd+=['--packed-veneers']
 if a.cpu_cache:cmd+=['--cpu-cache',str(a.cpu_cache),'--cpu-cache-control']
 if a.jit_reserve_mib!=16:cmd+=['--jit-reserve-mib',str(a.jit_reserve_mib)]
 report=dict(packed_veneers=a.packed_veneers,cpu_cache=str(a.cpu_cache)if a.cpu_cache else None,cpu_cache_sha256=hashlib.sha256(a.cpu_cache.read_bytes()).hexdigest()if a.cpu_cache else None,jit_initial_reserve_mib=a.jit_reserve_mib,scope='file input invokes same quickload request as F3; no physical key/visual/FPS claim, fresh Shared QA only',passed=False,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),events=[])
 print('Evidence:',e,flush=True);case=None;sent=False;ready_at=None
 with(e/'supervisor.log').open('w')as log:
  child=subprocess.Popen(cmd,cwd=P,stdout=log,stderr=subprocess.STDOUT)
  try:
   while child.poll()is None:
    if case is None:
     fresh=[f for f in(P/'evidence').glob('game-*')if f not in old and(f/'runtime.log').exists()]
     if len(fresh)==1:case=fresh[0];report['case']=str(case.relative_to(P))
    if case:
     text=(case/'runtime.log').read_text(errors='replace')
     if ready_at is None and'character ready; automatic menu input released'in text:
      ready_at=time.monotonic();report['events'].append(dict(kind='first-ready',utc_ns=time.time_ns()));print('first ready',flush=True)
     if ready_at is not None and not sent and time.monotonic()-ready_at>5:
      temp=pad.with_suffix('.tmp');temp.write_text('quickload\n');os.replace(temp,pad);sent=True
      report['events'].append(dict(kind='quickload-request',utc_ns=time.time_ns()));print('F3 requested',flush=True)
     elif sent and pad.read_text()=='quickload\n' and time.monotonic()-ready_at>5.3:
      temp=pad.with_suffix('.tmp');temp.write_text('\n');os.replace(temp,pad)
    time.sleep(.05)
  finally:
   if child.poll()is None:
    # Let the owner's existing bounded supervisor reap its positive PIDs.
    child.wait(timeout=110)
 report['supervisor_exit']=child.returncode
 if case and(case/'result.json').exists():
  r=json.loads((case/'result.json').read_text());text=(case/'runtime.log').read_text(errors='replace')
  report.update(stopped=r['stopped'],runtime_exit=r['exit'],generations=r['generations'],ready_count=text.count('character ready; automatic menu input released'),memory_policy=r['memory_policy'])
  report['packed_generation_installs']=text.count('ARM VENEER layout=darwin-packed-guest-ro-v1 ')
  report['cache_installs']=text.count('ARM CPU CACHE installed ');report['cached_dispatch_generations']=text.count('ARM CPU CACHE precompiled_dispatch_verified=1 slow_resolution=1')
  report['passed']=(not a.packed_veneers or report['packed_generation_installs']==2)and(not a.cpu_cache or(report['cache_installs']==2 and report['cached_dispatch_generations']==2))and hashlib.sha256(binary.read_bytes()).hexdigest()==report['binary_sha256']and child.returncode==0 and r['exit']==0 and r['stopped']=='time budget' and len(r['generations'])==1 and report['ready_count']==2
 (e/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2));return 0 if report['passed']else 1
if __name__=='__main__':raise SystemExit(main())
