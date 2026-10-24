#!/usr/bin/env python3
"""Bounded 2s hook timing + two texture metadata snapshots; never performance evidence."""
from pathlib import Path
import argparse,collections,json,os,subprocess,sys,time,csv
from analyze_stutter import read
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--window-ms',type=float,default=20);a=ap.parse_args();assert 0<a.window_ms<=2000
 pad=P/'state'/f'resource-diag-pad-{time.time_ns()}.txt';pad.write_text('')
 before={p for p in(P/'evidence').glob('game-*')if p.is_dir()};e=None;ready=None;opened=None;closed=None;requests=set()
 command=[sys.executable,str(P/'tools/run_game.py'),'--resource-hooks','--lifetime-audit','--stutter-audit','--boot',str(P.parent/'assets/boot-linked.bin'),'--app0',str(P.parent/'assets/CUSA03173-1.09'),'--content-profile',str(P.parent/'assets/content.bin'),'--seed-user',str(P.parent/'qa-seed-user'),'--clone-private-session',str(P/'state/game-1792762608095377000'),'--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--graphics','--quicksave','--frame-timing','--pad-file',str(pad),'--seconds','80']
 console=P/'evidence'/f'resource-diagnostic-{time.time_ns()}.log'
 with console.open('w')as output:
  process=subprocess.Popen(command,cwd=P,stdout=output,stderr=output)
  while process.poll() is None:
   if e is None:
    fresh=[p for p in(P/'evidence').glob('game-*')if p.is_dir()and p not in before and(p/'runtime.log').exists()]
    if len(fresh)==1:e=fresh[0];print('Evidence:',e,flush=True)
   now=time.monotonic()
   if e:
    if ready is None and 'character ready; automatic menu input released' in(e/'runtime.log').read_text(errors='replace'):ready=now
    if ready is not None:
     for sequence,delay in [(1,22),(2,45)]:
      if now>=ready+delay and sequence not in requests:
       root=e/'texture-lifetimes';temp=root/'request.tmp';temp.write_text(str(sequence)+'\n');os.replace(temp,root/'request');requests.add(sequence)
     if opened is None and now>=ready+30:
      with(e/'stutter-control.bin').open('r+b')as f:f.write(b'\x01\x00\x00\x00')
      opened=time.clock_gettime_ns(time.CLOCK_MONOTONIC)
     if opened is not None and closed is None and time.clock_gettime_ns(time.CLOCK_MONOTONIC)>=opened+int(a.window_ms*1e6):
      with(e/'stutter-control.bin').open('r+b')as f:f.write(bytes(4))
      closed=time.clock_gettime_ns(time.CLOCK_MONOTONIC)
   time.sleep(.001 if opened is not None and closed is None else .05)
 if process.returncode:raise RuntimeError(f'private supervisor failed: {console}')
 assert e is not None and ready is not None and opened and closed,'no completed diagnostic window'
 runtime=json.loads((e/'result.json').read_text());assert runtime['exit']==0 and runtime['stopped']=='time budget'and not runtime['cleanup_warnings']
 traces=[read(p)for p in e.glob('stutter-*.bin')if p.name.split('-')[-1].split('.')[0].isdigit()]
 rows=[r for t in traces for r in t['rows']if r['kind']==33 and r['start']>=opened and r['end']<=closed]
 assert all(t['dropped']==0 and t['overwritten']==0 for t in traces),'timing trace incomplete; do not use retained subset as total'
 names=['copy','range_alloc','heap_return','range_free','heap_free','heap_plain','heap_aligned'];timing={}
 for tag in sorted({r['arg']for r in rows}):
  rs=[r for r in rows if r['arg']==tag];name=f'{names[tag&255]}_nest{tag>>8}'
  timing[name]=dict(calls=len(rs),wall_sum_ms=sum(r['wall']for r in rs)/1e6,cpu_sum_ms=sum(r['cpu']for r in rs)/1e6,max_wall_ms=max(r['wall']for r in rs)/1e6)
 snapshots=[]
 for path in sorted((e/'texture-lifetimes').glob('textures-*.csv')):
  raw=list(csv.DictReader(path.open()));assert raw and all(None not in r and all(v is not None for v in r.values())for r in raw),'invalid texture CSV'
  assert len(raw[0])==40,'unexpected metadata schema'
  textures=[r for r in raw if int(r['usage'])&1 and not(int(r['usage'])&12)]
  def bucket(r):
   if int(r['current_owner']):return 'covering_live_owner'
   if int(r['heap_live_overlaps']):return 'partial_heap_overlap'
   if int(r['range_live_overlaps']):return 'partial_range_overlap'
   return 'no_tracked_live_overlap'
  groups={}
  for row in textures:
   name=bucket(row);entry=groups.setdefault(name,dict(images=0,host_bytes=0,guest_bytes=0));entry['images']+=1;entry['host_bytes']+=int(row['host_bytes']);entry['guest_bytes']+=int(row['guest_bytes'])
  snapshots.append(dict(path=str(path),all_images=len(raw),sampled_non_target=groups,largest_unknown=sorted([r for r in textures if not int(r['current_owner'])],key=lambda r:int(r['host_bytes']),reverse=True)[:8]))
 assert len(snapshots)==2,'missing snapshot'
 report=dict(evidence=str(e),scope='diagnostic timing/metadata only, no FPS comparison, zero-gain SDL, no GUI capture',window=dict(open_mono_ns=opened,close_mono_ns=closed,seconds=(closed-opened)/1e9),traces=[{k:v for k,v in t.items()if k!='rows'}for t in traces],hook_timing=timing,snapshots=snapshots,warning='Nested wrappers and concurrent threads overlap; sum is not frame latency. Unknown/no current overlap does not prove a resource was freed. No collection or budget change.')
 (e/'resource-diagnostic-summary.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:v for k,v in report.items()if k!='snapshots'},indent=2));print('Snapshots:',json.dumps([dict(path=s['path'],sampled_non_target=s['sampled_non_target'])for s in snapshots],indent=2))
if __name__=='__main__':main()
