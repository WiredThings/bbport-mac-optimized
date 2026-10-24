#!/usr/bin/env python3
"""One same-scene kernel memory snapshot per private CPU engine; never an FPS test."""
from pathlib import Path
import argparse,subprocess,sys,time,json,hashlib
from native_observer import NativeObserver
from memory_categories import parse
from fex_map_provenance import associate
P=Path(__file__).resolve().parents[1]
GPU=P/'backend/bbport/bin/Bloodborne Metal.app/Contents/MacOS/bb-gpu'
def targets(observer,owner,engine):
 cpu_path=P/('build/fex/Bin/bb-game-arm64'if engine=='arm64'else'rosetta-reference/bin/bb-probe-metal')
 found=[]
 for supervisor,_,_ in observer.processes(owner):
  if supervisor==owner:continue
  for pid,_,path in observer.processes(supervisor):
   if path==str(cpu_path):found.append(pid)
 if len(found)!=1:raise RuntimeError('private CPU identity is ambiguous')
 cpu=found[0];gpu=[pid for pid,_,path in observer.processes(cpu)if pid!=cpu and path==str(GPU)]
 if len(gpu)!=1:raise RuntimeError('private GPU identity is ambiguous')
 return {cpu:str(cpu_path),gpu[0]:str(GPU)}
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--engines',nargs='+',choices=['arm64','rosetta'],default=['arm64','rosetta']);ap.add_argument('--packed-veneers',action='store_true');ap.add_argument('--hot-staging-reuse',action='store_true');ap.add_argument('--seconds',type=int,default=110);ap.add_argument('--snapshot-delay-seconds',type=int,default=45);ap.add_argument('--fex-mapping-audit',action='store_true');a=ap.parse_args();assert not a.fex_mapping_audit or a.engines==['arm64'],'mapping provenance only for standalone ARM64 memory cohort'
 assert len(a.engines)==len(set(a.engines))
 assert 90<=a.seconds<=300 and 45<=a.snapshot_delay_seconds<=120 and a.snapshot_delay_seconds+50<=a.seconds,'leave a bounded capture/cleanup margin'
 report_dir=P/'evidence'/f'memory-categories-{time.time_ns()}';report_dir.mkdir();report=dict(scope='private fixed scene, silent, kernel memory tools only; frames in this run are not performance evidence',performance_accepted=False,packed_veneers=a.packed_veneers,hot_staging_reuse=a.hot_staging_reuse,seconds=a.seconds,snapshot_delay_seconds=a.snapshot_delay_seconds,fex_mapping_audit=a.fex_mapping_audit,runs=[])
 files=[P/'build/fex/Bin/bb-game-arm64',P/'rosetta-reference/bin/bb-probe-metal',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib'];report['artifact_sha256']={str(p):hashlib.file_digest(p.open('rb'),'sha256').hexdigest()for p in files}
 print('Evidence:',report_dir,flush=True)
 with NativeContext()as n:
  for engine in a.engines:
   assert report['artifact_sha256']=={str(p):hashlib.file_digest(p.open('rb'),'sha256').hexdigest()for p in files}
   preflight=n.system();assert not preflight['locked']and preflight['pressure']!=4,'locked or severe pressure, no game started'
   old=set((P/'evidence').glob('game-*'));cmd=[sys.executable,str(P/'tools/run_stutter_case.py'),'--engine',engine,'--native-observer','--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--seconds',str(a.seconds),'--cycles','2']
   if a.packed_veneers and engine=='arm64':cmd+=['--packed-veneers']
   if a.hot_staging_reuse:cmd+=['--hot-staging-reuse']
   if a.fex_mapping_audit:cmd+=['--fex-mapping-audit']
   if engine=='arm64':cmd+=['--resource-hooks','--resource-fast-dispatch','--resource-leaf-dispatch','--extra-heaps']
   with (report_dir/f'{engine}-supervisor.log').open('w')as log:
    proc=subprocess.Popen(cmd,stdout=log,stderr=log,cwd=P);e=None;ready=None;attempted=False;snapshot=None;error=None
    while proc.poll()is None:
     if e is None:
      found=[p for p in (P/'evidence').glob('game-*')if p not in old and(p/'runtime.log').exists()]
      if len(found)==1:e=found[0];print(engine,'case',e,flush=True)
     if e and ready is None and'character ready; automatic menu input released'in(e/'runtime.log').read_text(errors='replace'):ready=time.monotonic()
     if ready and not attempted and time.monotonic()-ready>=a.snapshot_delay_seconds:
      attempted=True
      try:
       system=n.system();assert not system['locked']and system['pressure']!=4,'protective memory state'
       identity=targets(n,proc.pid,engine);start=time.clock_gettime_ns(time.CLOCK_MONOTONIC);prefix=report_dir/engine
       for pid,path in identity.items():assert n.path(pid)==path,'process path changed'
       mapping_before=(e/'runtime.log').read_text()if a.fex_mapping_audit else None
       if mapping_before is not None:prefix.with_suffix('.mapping-before.log').write_text(mapping_before)
       output=prefix.with_suffix('.footprint.json');args=['/usr/bin/footprint','-j',str(output),'-f','bytes','--swapped','--wired']
       for pid in identity:args+=['-p',str(pid)]
       with prefix.with_suffix('.footprint.log').open('w')as out:subprocess.run(args,stdout=out,stderr=subprocess.STDOUT,timeout=15,check=True)
       parsed=parse(json.loads(output.read_text()),{pid:'gpu'if path==str(GPU)else'game'for pid,path in identity.items()})
       for pid,path in identity.items():
        assert n.path(pid)==path,'process path changed after snapshot'
        with (report_dir/f'{engine}-{pid}-vmmap.txt').open('w')as out:subprocess.run(['/usr/bin/vmmap','-summary','-w',str(pid)],stdout=out,stderr=subprocess.STDOUT,timeout=10,check=True)
       detailed=None;cpu_pid=next(pid for pid,path in identity.items()if path!=str(GPU))
       if a.fex_mapping_audit:
        detailed=report_dir/f'{engine}-{cpu_pid}-regions.txt'
        with detailed.open('w')as out:subprocess.run(['/usr/bin/vmmap','-w','-pages','-interleaved','-noCoalesce',str(cpu_pid)],stdout=out,stderr=subprocess.STDOUT,timeout=15,check=True)
       kernel_end=time.clock_gettime_ns(time.CLOCK_MONOTONIC)
       mapping_after=None
       if a.fex_mapping_audit:
        deadline=time.monotonic()+3
        while time.monotonic()<deadline:
         mapping_after=(e/'runtime.log').read_text()
         if any(int(p)==cpu_pid and int(t)>=kernel_end for p,t in __import__('re').findall(r'^FEX_MAP_CHECKPOINT pid=(\d+) mono=(\d+) issued=\d+ failed=\d+$',mapping_after,__import__('re').M)):break
         time.sleep(.05)

       if mapping_after is not None:prefix.with_suffix('.mapping-after.log').write_text(mapping_after)
       provenance=associate(mapping_before,mapping_after,detailed.read_text(),cpu_pid,required_after_mono_ns=kernel_end)if detailed else None
       for pid,path in identity.items():assert n.path(pid)==path,'process exited/changed during maps'
       end=time.clock_gettime_ns(time.CLOCK_MONOTONIC);snapshot=dict(start_mono_ns=start,end_mono_ns=end,cost_seconds=(end-start)/1e9,identities=identity,system=system,parsed=parsed,fex_provenance=provenance);prefix.with_suffix('.parsed.json').write_text(json.dumps(snapshot,indent=2)+'\n');print(engine,'memory snapshot complete',flush=True)
      except Exception as exc:error=repr(exc);print(engine,'snapshot error',error,flush=True)
     time.sleep(.1)
   runtime=json.loads((e/'result.json').read_text())if e and(e/'result.json').exists()else{};row=dict(engine=engine,evidence=str(e)if e else None,supervisor_exit=proc.returncode,stopped=runtime.get('stopped'),exit=runtime.get('exit'),snapshot=snapshot,error=error,performance_accepted=False);report['runs'].append(row);(report_dir/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(engine,'terminal',row['stopped'],flush=True)
   assert proc.returncode==0 and row['exit']==0 and row['stopped']=='time budget'and snapshot and not error,'memory cohort incomplete; not accepted'
 report['complete']=True;(report_dir/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('memory cohort complete',report_dir,flush=True)
class NativeContext:
 def __enter__(self):self.n=NativeObserver();return self.n
 def __exit__(self,*args):self.n.close()
if __name__=='__main__':main()
