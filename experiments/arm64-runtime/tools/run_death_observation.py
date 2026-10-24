#!/usr/bin/env python3
"""Private, silent death/load/control compatibility session; no automatic pass claim."""
from pathlib import Path
import argparse,json,subprocess,sys,time
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--packed-veneers',action='store_true');ap.add_argument('--hot-staging-reuse',action='store_true');ap.add_argument('--seconds',type=int,default=600);ap.add_argument('--normal-save',action='store_true');ap.add_argument('--source-state',type=Path,default=P/'state/game-1792762608095377000');options=ap.parse_args()
 assert 120<=options.seconds<=600
 source=options.source_state.resolve();assert source.parent==P/'state' and source.is_dir() and not options.source_state.is_symlink(),'only an existing isolated private state may seed death QA'
 e=P/'evidence'/f'death-observation-{time.time_ns()}';e.mkdir();pad=P/'state'/f'death-pad-{time.time_ns()}.txt';pad.write_text('')
 old=set((P/'evidence').glob('game-*'));report=dict(scope='private clone only, same graphics/lifetime hooks, silent, not a performance test; death cause is irrelevant; verify load, returned scene and post-return control',passed=False,normal_save=options.normal_save,source_state=str(source),packed_veneers=options.packed_veneers,hot_staging_reuse=options.hot_staging_reuse,pad=str(pad),events=[]);print('Death evidence:',e,flush=True);print('Pad:',pad,flush=True)
 cmd=[sys.executable,str(P/'tools/run_game.py'),'--boot',str(P.parent/'assets/boot-linked.bin'),'--app0',str(P.parent/'assets/CUSA03173-1.09'),'--content-profile',str(P.parent/'assets/content.bin'),'--seed-user',str(P.parent/'qa-seed-user'),'--clone-private-session',str(source),'--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--graphics','--frame-timing','--pad-file',str(pad),'--seconds',str(options.seconds),'--native-observer','--resource-hooks','--resource-fast-dispatch','--resource-leaf-dispatch','--extra-heaps']
 cmd+=['--dev-world-warper'] if options.normal_save else ['--quicksave']
 if options.packed_veneers:cmd+=['--packed-veneers']
 if options.hot_staging_reuse:cmd+=['--hot-staging-reuse']
 case=None;ready=False
 with(e/'supervisor.log').open('w')as log:
  child=subprocess.Popen(cmd,cwd=P,stdout=log,stderr=subprocess.STDOUT);report['owner_pid']=child.pid
  while child.poll()is None:
   if case is None:
    fresh=[f for f in(P/'evidence').glob('game-*')if f not in old and(f/'runtime.log').exists()]
    if len(fresh)==1:case=fresh[0];report['case']=str(case.relative_to(P));print('Case:',case,flush=True)
   if case and not ready and'character ready; automatic menu input released'in(case/'runtime.log').read_text(errors='replace'):
    ready=True;report['events'].append(dict(event='initial-character-ready',utc_ns=time.time_ns()));print('Character ready',flush=True)
   (e/'session.json').write_text(json.dumps(report,indent=2)+'\n');time.sleep(.2)
 report['supervisor_exit']=child.returncode
 if case and(case/'result.json').exists():
  policy=json.loads((case/'cpu-policy.json').read_text());assert policy['packed_veneers']==options.packed_veneers
  r=json.loads((case/'result.json').read_text());report['terminal']={k:r[k]for k in ('exit','stopped','seconds','cleanup_warnings')}
 terminal=report.get('terminal');report['session_completed_normally']=bool(terminal and terminal['exit']==0 and terminal['stopped']in('time budget','private test completed')and not terminal['cleanup_warnings'])
 (e/'session.json').write_text(json.dumps(report,indent=2)+'\n');print('Terminal:',terminal,flush=True)
 if not report['session_completed_normally']:raise RuntimeError('isolated observation did not complete normally; inspect its supervisor.log; this is not a death/load/control pass')
if __name__=='__main__':main()
