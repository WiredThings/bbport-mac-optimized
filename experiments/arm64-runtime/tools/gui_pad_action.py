#!/usr/bin/env python3
"""Bounded existing pad API for a live, guarded original GUI QA session."""
from pathlib import Path
import argparse,json,os,re,time
from native_observer import NativeObserver
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('case',type=Path);ap.add_argument('tokens',nargs='+');ap.add_argument('--seconds',type=float,default=.18);a=ap.parse_args()
 e=a.case.resolve();assert e.parent==P/'evidence'and .05<=a.seconds<=10 and len(a.tokens)<=12
 c=json.loads((e/'case.json').read_text());g=json.loads((e/'guard-ready.json').read_text());assert not(e/'guard-result.json').exists(),'session terminal'
 assert Path(c['app']).is_relative_to(P/'apps')and c['bundle_id'].startswith('io.github.bbport.optimized.options-qa.arm64.gui.')
 assert g['path']==c['app']+'/Contents/MacOS/bbport'
 o=NativeObserver();assert o.path(g['pid'])==g['path'],'GUI identity changed';assert not o.system()['locked'];o.close()
 p=Path(c['pad']);assert p.is_relative_to(P/'state')and p.is_file()and not p.is_symlink()
 buttons={'cross','circle','square','triangle','l1','r1','l2','r2','l3','r3','options','touchpad','touchpad_left','touchpad_right','up','down','left','right','quicksave','quickload'}
 for token in a.tokens:assert all(x in buttons or re.fullmatch(r'[lr][xy]=(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)',x)for x in token.split())
 def write(value):
  t=p.with_name(p.name+'.action-'+str(os.getpid()));assert not t.exists();fd=os.open(t,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
  with os.fdopen(fd,'w')as f:f.write(value)
  os.replace(t,p)
 for token in a.tokens:
  event={'token':token,'seconds':a.seconds,'start_utc_ns':time.time_ns()}
  try:write(token+'\n');time.sleep(a.seconds)
  finally:
   write('');event['neutral_utc_ns']=time.time_ns()
   with(e/'actions.jsonl').open('a')as f:f.write(json.dumps(event)+'\n')
  time.sleep(.35)
if __name__=='__main__':main()
