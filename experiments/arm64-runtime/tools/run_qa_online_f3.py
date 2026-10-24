#!/usr/bin/env python3
"""Existing QA credentials, normal online boot -> same F3 request -> offline restore.
No visual automation, audio, FPS claim, formal account or formal data.
"""
from pathlib import Path
import json,subprocess,sys,time,os,re,hashlib
P=Path(__file__).resolve().parents[1]
def main():
 e=P/'evidence'/f'qa-online-f3-{time.time_ns()}';e.mkdir();pad=P/'state'/f'{e.name}-pad.txt';pad.write_text('')
 prior=set((P/'evidence').glob('game-*'))
 cmd=[sys.executable,str(P/'tools/run_game.py'),'--boot',str(P.parent/'assets/boot-linked.bin'),'--app0',str(P.parent/'assets/CUSA03173-1.09'),'--content-profile',str(P.parent/'assets/content.bin'),'--seed-user',str(P.parent/'qa-seed-user'),'--clone-private-session',str(P/'state/game-1792762608095377000'),'--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--graphics','--quicksave','--qa-online','--pad-file',str(pad),'--seconds','180','--native-observer','--resource-hooks','--resource-fast-dispatch','--resource-leaf-dispatch','--extra-heaps','--packed-veneers','--hot-staging-reuse']
 report=dict(scope='existing private QA account; normal online boot then F3 offline checkpoint; no physical key/visual/FPS claim',passed=False,events=[],command=cmd)
 files=['build/fex/Bin/bb-game-arm64','backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib','build/bbnet-arm64/libbbnet.dylib'];report['artifact_sha256']={s:hashlib.sha256((P/s).read_bytes()).hexdigest()for s in files}
 print('Evidence:',e,flush=True);case=None;sent=None;ready=None
 with(e/'supervisor.log').open('w')as log:
  child=subprocess.Popen(cmd,cwd=P,stdout=log,stderr=subprocess.STDOUT)
  try:
   while child.poll()is None:
    if case is None:
     fresh=[f for f in(P/'evidence').glob('game-*')if f not in prior and(f/'runtime.log').exists()]
     if len(fresh)==1:case=fresh[0];report['case']=str(case.relative_to(P))
    if case:
     text=(case/'runtime.log').read_text(errors='replace')
     replies=re.findall(r'ARM NET RESPONSE service=context_start http=(\d+) result_kind=(-?\d+) bytes=(\d+)',text)
     accepted=any(200<=int(status)<300 and int(kind)==0 for status,kind,size in replies) and bool(re.search(r'ARM NET RESPONSE service=game_http http=2[0-9][0-9] ',text))
     if ready is None and accepted and'character ready; automatic menu input released'in text:
      ready=time.monotonic();report['events'].append(dict(kind='context-server-accepted-and-character-ready',utc_ns=time.time_ns()));print('server context accepted and character ready',flush=True)
     if ready is not None and sent is None and time.monotonic()-ready>10:
      temp=pad.with_suffix('.tmp');temp.write_text('quickload\n');os.replace(temp,pad);sent=time.monotonic();report['events'].append(dict(kind='F3-request',utc_ns=time.time_ns()));print('F3 requested',flush=True)
     elif sent and time.monotonic()-sent>.3 and pad.read_text()=='quickload\n':
      temp=pad.with_suffix('.tmp');temp.write_text('');os.replace(temp,pad)
    time.sleep(.1)
  finally:
   if child.poll()is None:child.wait(timeout=190)
 if case and(case/'result.json').is_file():
  r=json.loads((case/'result.json').read_text());text=(case/'runtime.log').read_text(errors='replace')
  responses=[dict(service=m[0],http=int(m[1]),result_kind=int(m[2]),bytes=int(m[3]))for m in re.findall(r'ARM NET RESPONSE service=([a-z_]+) http=(\d+) result_kind=(-?\d+) bytes=(\d+)',text)]
  report.update(responses=responses,stopped=r['stopped'],runtime_exit=r['exit'],cleanup_warnings=r['cleanup_warnings'],generations=r['generations'],character_ready_count=text.count('character ready; automatic menu input released'),network_initializations=text.count('typed guest bridge ready'),HLE_stops=text.count('ARM HLE STOP'),memory_policy=r['memory_policy'])
  s=r['samples'];report.update(pressure_max=max(x['pressure']for x in s),swap_delta_mib=s[-1]['swap_mib']-s[0]['swap_mib'])
  report['context_server_accepted']=any(x['service']=='context_start'and 200<=x['http']<300 and x['result_kind']==0 for x in responses)
  report['game_http_success']=any(x['service']=='game_http'and 200<=x['http']<300 for x in responses)
  report['passed']=report['game_http_success'] and child.returncode==0 and r['exit']==0 and r['stopped']=='time budget'and report['context_server_accepted']and len(r['generations'])==1 and report['character_ready_count']==2 and report['network_initializations']==1 and report['HLE_stops']==0 and not r['cleanup_warnings']
  report['unchanged_binaries']=all(hashlib.sha256((P/s).read_bytes()).hexdigest()==h for s,h in report['artifact_sha256'].items());report['passed']&=report['unchanged_binaries']
 (e/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items()if k!='command'},indent=2));return 0 if report['passed']else 1
if __name__=='__main__':raise SystemExit(main())
