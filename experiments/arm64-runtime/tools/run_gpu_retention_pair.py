#!/usr/bin/env python3
"""Existing Metal lifetime/staging diagnostics, two matched private engines.
No resource payload export, visual capture, budget change or FPS conclusion.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,time
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--engines',nargs='+',choices=['arm64','rosetta'],default=['arm64','rosetta']);ap.add_argument('--hot-staging-reuse',action='store_true');args=ap.parse_args();assert len(args.engines)==len(set(args.engines))
 e=P/'evidence'/f'gpu-retention-pair-{time.time_ns()}';e.mkdir()
 artifacts=[P/'build/fex/Bin/bb-game-arm64',P/'rosetta-reference/bin/bb-probe-metal',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib']
 def hashes():return {str(f):hashlib.file_digest(f.open('rb'),'sha256').hexdigest()for f in artifacts}
 frozen=hashes();report=dict(scope='same private checkpoint, original graphics, same ARM64 Metal backend; native metadata only, staging audit; explicitly not FPS evidence',artifact_sha256=frozen,hot_staging_reuse=args.hot_staging_reuse,runs=[],complete=False,performance_accepted=False,formal_data_modified=False)
 def save():(e/'result.json').write_text(json.dumps(report,indent=2)+'\n')
 save();print('Retention evidence:',e,flush=True)
 for engine in args.engines:
  assert hashes()==frozen
  trigger=P/'state'/f'retention-capture-{engine}-{time.time_ns()}';assert not trigger.exists()
  old=set((P/'evidence').glob('game-*'));case=None;ready=None;requested=[];error=None
  cmd=[sys.executable,str(P/'tools/run_stutter_case.py'),'--engine',engine,'--native-observer','--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--seconds','160','--cycles','2','--staging-audit','--capture-trigger',str(trigger)]
  if args.hot_staging_reuse:cmd+=['--hot-staging-reuse']
  if engine=='arm64':cmd+=['--packed-veneers','--resource-hooks','--resource-fast-dispatch','--resource-leaf-dispatch','--extra-heaps']
  with (e/(engine+'.log')).open('w')as output:
   proc=subprocess.Popen(cmd,cwd=P,stdout=output,stderr=subprocess.STDOUT)
   try:
    while proc.poll()is None:
     if case is None:
      found=[q for q in(P/'evidence').glob('game-*')if q not in old and(q/'runtime.log').is_file()]
      if len(found)==1:case=found[0];print(engine,'case',case,flush=True)
     if case:
      text=(case/'runtime.log').read_text(errors='replace')
      if ready is None and 'character ready; automatic menu input released'in text:ready=time.monotonic()
      index=len(requested)
      if ready is not None and index<2 and time.monotonic()-ready>=(40,110)[index]:
       assert not trigger.exists(),'previous capture trigger not consumed'
       if index:assert len(list(case.glob('frame_*.txt')))>=index,'previous native frame not completed'
       pending=trigger.with_suffix('.pending');pending.write_text('metadata only\n');os.replace(pending,trigger)
       requested.append(dict(sequence=index+1,delay_from_ready_s=time.monotonic()-ready,monotonic_ns=time.clock_gettime_ns(time.CLOCK_MONOTONIC)));print(engine,'requested capture',index+1,flush=True)
     time.sleep(.1)
   except BaseException as exc:error=repr(exc)
   finally:
    if proc.poll()is None:proc.wait(timeout=175)
  runtime=json.loads((case/'result.json').read_text())if case and(case/'result.json').is_file()else{}
  captures=sorted(case.glob('frame_*.txt'))if case else[]
  runtime_complete=proc.returncode==0 and runtime.get('exit')==0 and runtime.get('stopped')=='time budget'and not runtime.get('cleanup_warnings')
  conditions=json.loads((case/'stutter-case-conditions.json').read_text())if case else{}
  policy=json.loads((case/'cpu-policy.json').read_text())if case else{}
  correct_policy=conditions.get('staging_audit')is True and conditions.get('native_frame_capture')is True and policy.get('packed_veneers')==(engine=='arm64')
  marker=(case/'runtime.log').read_text(errors='replace').count('Metal staging recent reuse: enabled;')if case else 0
  correct_policy=correct_policy and conditions.get('hot_staging_reuse')==args.hot_staging_reuse and bool(marker)==args.hot_staging_reuse
  row=dict(hot_staging_reuse=args.hot_staging_reuse,activation_markers=marker,engine=engine,case=str(case)if case else None,runtime_complete=runtime_complete,correct_policy=correct_policy,exit=runtime.get('exit'),stopped=runtime.get('stopped'),seconds=runtime.get('seconds'),requested=requested,captures=[str(q)for q in captures],error=error,inventory_accepted=False,full_frame_accepted=False,performance_accepted=False)
  report['runs'].append(row);assert hashes()==frozen;save();print(engine,'terminal',row['stopped'],'capture files',len(captures),flush=True)
  assert runtime_complete and correct_policy and not error and len(requested)==len(captures)==2,'incomplete metadata cohort; do not infer missing resources'
 report['complete']=True;report['analysis_pending']=True;save();print('Completed metadata pair:',e,flush=True)
if __name__=='__main__':main()
