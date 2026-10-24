#!/usr/bin/env python3
"""Bounded existing runtime test-controller input; fresh private death-session only."""
from pathlib import Path
import argparse,json,os,re,time
P=Path(__file__).resolve().parents[1]
def main():
 a=argparse.ArgumentParser();a.add_argument('session',type=Path);a.add_argument('token');a.add_argument('--seconds',type=float,default=1);x=a.parse_args();d=x.session.resolve();assert d.parent==P/'evidence'and d.name.startswith('death-observation-')and 0.05<=x.seconds<=10
 r=json.loads((d/'session.json').read_text());assert 'terminal'not in r;os.kill(r['owner_pid'],0);pad=Path(r['pad']);assert pad.parent==P/'state'and pad.name.startswith('death-pad-')and pad.is_file()and not pad.is_symlink()
 buttons={'cross','circle','square','triangle','l1','r1','l2','r2','l3','r3','options','up','down','left','right','quickload'}
 assert all(t in buttons or re.fullmatch(r'[lr][xy]=(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)',t)for t in x.token.split())
 def write(text):
  temp=pad.with_suffix('.pending');temp.write_text(text+'\n');os.replace(temp,pad)
 event=dict(token=x.token,seconds=x.seconds,start_utc_ns=time.time_ns(),start_mono_ns=time.clock_gettime_ns(time.CLOCK_MONOTONIC))
 try:write(x.token);time.sleep(x.seconds)
 finally:write('');event['neutral_utc_ns']=time.time_ns();(d/'actions.jsonl').open('a').write(json.dumps(event)+'\n')
 print(json.dumps(event))
if __name__=='__main__':main()
