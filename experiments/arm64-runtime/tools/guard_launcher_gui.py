#!/usr/bin/env python3
"""Guard a specifically identified private GUI's game descendants, never name-match kill."""
from pathlib import Path
import argparse,os,sys,time,json,signal
from native_observer import NativeObserver
from qa_memory_policy import MemoryPolicy
P=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('evidence',type=Path);ap.add_argument('pid',type=int);ap.add_argument('--seconds',type=int,default=600);args=ap.parse_args();assert 60<=args.seconds<=1800
e=args.evidence;pid=args.pid;assert e.parent==P/'evidence'
c=json.loads((e/'case.json').read_text());expected=c['app']+'/Contents/MacOS/bbport'
obs=NativeObserver();assert obs.path(pid)==expected
before=obs.system();assert not before['locked']and before['pressure']!=4
policy=MemoryPolicy(before);start=time.monotonic();tracked={};samples=[];reason=None
(e/'guard-ready.json').write_text(json.dumps(dict(pid=pid,path=expected,before=before,policy=MemoryPolicy.MANIFEST,time_budget_seconds=args.seconds)))
print('guard ready',flush=True)
def children():
 pending=[pid];seen=set();result={}
 while pending:
  parent=pending.pop()
  if parent in seen:continue
  seen.add(parent);assert len(seen)<4096
  for child,ppid,path in obs.processes(parent):
   if child==pid:continue
   result[child]=(child,ppid,path);tracked[child]=path
   if child!=parent and child not in seen:pending.append(child)
 return list(result.values())
def stop_owned():
 for sig in (signal.SIGTERM,signal.SIGKILL):
  for child,path in reversed(list(tracked.items())):
   if obs.path(child)==path:
    try:os.kill(child,sig)
    except ProcessLookupError:pass
  if sig==signal.SIGTERM:time.sleep(2)
try:
 while obs.path(pid)==expected:
  s=obs.system();s.update(seconds=time.monotonic()-start,processes=children());samples.append(s)
  reason=policy.observe(s['seconds'],s)
  if s['seconds']>=args.seconds:reason='time budget'
  if (e/'guard-finish').exists():reason='QA finished'
  if reason:break
  time.sleep(.25)
finally:
 stop_owned()
 result=dict(reason=reason or 'GUI exited',seconds=time.monotonic()-start,before=before,after=obs.system(),policy=MemoryPolicy.MANIFEST,samples=samples,owner=pid)
 (e/'guard-result.json').write_text(json.dumps(result,indent=2)+'\n');obs.close();print(json.dumps({k:v for k,v in result.items()if k!='samples'}),flush=True)
