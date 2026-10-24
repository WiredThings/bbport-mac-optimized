#!/usr/bin/env python3
"""Silent isolated real-game static-code profile. Never a performance measurement."""
from pathlib import Path
import subprocess,time,json,hashlib,sys,argparse
from cpu_code_map import parse
from native_observer import NativeObserver
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--jit-reserve-mib',type=int,choices=[16,128],default=16);a=ap.parse_args()
 e=P/'evidence'/f'cpu-code-profile-{time.time_ns()}';e.mkdir();n=NativeObserver()
 try:pre=n.system()
 finally:n.close()
 report=dict(scope='Shared QA static executable identity/entry coverage only; no CPU disk-cache load and no FPS claim',preflight=pre,jit_initial_reserve_mib=a.jit_reserve_mib,passed=False)
 if pre['locked']or pre['pressure']==4:
  report['blocked']='screen locked or severe pressure; no game started';(e/'result.json').write_text(json.dumps(report,indent=2));print(e);raise SystemExit(2)
 old=set((P/'evidence').glob('game-*'))
 cmd=[sys.executable,str(P/'tools/run_stutter_case.py'),'--cpu-code-map','--native-observer','--resource-hooks','--resource-fast-dispatch','--resource-leaf-dispatch','--extra-heaps','--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--seconds','110','--cycles','1']
 if a.jit_reserve_mib!=16:cmd+=['--jit-reserve-mib',str(a.jit_reserve_mib)]
 print('Evidence:',e,flush=True)
 with(e/'supervisor.log').open('w')as log:proc=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,cwd=P)
 report['supervisor_exit']=proc.returncode;cases=[p for p in(P/'evidence').glob('game-*')if p not in old]
 if len(cases)==1 and(cases[0]/'result.json').exists():
  case=cases[0];r=json.loads((case/'result.json').read_text());report.update(case=str(case.relative_to(P)),runtime_exit=r['exit'],stopped=r['stopped'])
  ready='character ready; automatic menu input released'in(case/'runtime.log').read_text(errors='replace');report['character_ready']=ready
  state=Path(r['state']);need=state.resolve().parent==P/'state';assert need,'private state identity'
  manifest=state/'cpu-code-manifest.json'
  if manifest.exists():
   m=json.loads(manifest.read_text());maps=list(state.glob('cpu-code-*.map'));report['manifest']=m
   rows=[]
   for f in maps:
    mapped=parse(f.read_bytes(),m);mapped['path']=str(f);rows.append(mapped)
   report['maps']=rows
   report['passed']=proc.returncode==0 and r['exit']==0 and r['stopped']=='time budget'and ready and bool(rows)and m['modified_pages']>0 and m['retained_hash_bytes']==0
  else:report['missing']='no static code manifest'
 else:report['missing']='no unambiguous completed game case'
 (e/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(dict(evidence=str(e),passed=report['passed'],case=report.get('case'),blocks=[x['unique_blocks']for x in report.get('maps',[])]),indent=2))
 if not report['passed']:raise SystemExit(1)
if __name__=='__main__':main()
