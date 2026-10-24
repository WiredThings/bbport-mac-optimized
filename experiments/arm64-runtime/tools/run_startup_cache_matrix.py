#!/usr/bin/env python3
"""Frozen, symmetric 2x2 startup-cache/JIT-reservation experiment, private QA only."""
from pathlib import Path
import argparse,hashlib,json,statistics,subprocess,sys,time,re,math
from run_hle_lookup_abba import complete_rows
P=Path(__file__).resolve().parents[1]

def percentile(values, p):
 values=sorted(values); x=(len(values)-1)*p; i=int(x)
 return values[i]+(values[min(i+1,len(values)-1)]-values[i])*(x-i)

def summarize(case,variant,index):
 r=json.loads((case/'result.json').read_text());c=json.loads((case/'stutter-case-conditions.json').read_text());log=(case/'runtime.log').read_text(errors='replace')
 events=[json.loads(x)for x in (case/'operator-events.jsonl').read_text().splitlines()]
 assert r['exit']==0 and r['stopped']=='time budget' and not r['cleanup_warnings'],r.get('stopped')
 assert r['cpu_time_schema_version']==2 and r['observer']=='native SDK APIs'
 assert c['native_observer'] and c['cpu_cache_control'] and c['cycles']==2 and c['extra_heaps']
 assert not any(c[k]for k in ('audit','sample_cpu','compile_audit','cpu_code_map','fex_mapping_audit','guest_frame_audit','guest_sync_audit','pipeline_archive','extra_heaps_fresh'))
 assert log.count('ARM EXTRA 5/5 actual heap hooks installed')==1
 assert log.count('benchmark stream muted; original audio backend and cadence retained')>=2
 assert 'ARM FATAL'not in log and 'ARM RESOURCE invalid metadata'not in log
 assert sum(x['event']=='character_ready'for x in events)==1 and sum(x['event']=='cycle_end'for x in events)==2
 cached=variant in ('cache','both');reserve=128 if variant in ('reserve','both')else 16
 assert c['jit_initial_reserve_mib']==reserve and bool(c['cpu_cache'])==cached
 installed=re.findall(r'ARM CPU CACHE installed file_bytes=(\d+) code_bytes=(\d+) blocks=(\d+) initial_jit_bytes=(\d+) software_tso=1',log)
 dispatch='ARM CPU CACHE precompiled_dispatch_verified=1 slow_resolution=1'in log
 assert bool(installed)==cached and dispatch==cached,'cache case did not execute precompiled code'
 assert 'ARM CPU CACHE rejected'not in log and 'ARM CPU CACHE ignored'not in log
 env=json.loads((Path(r['state'])/'cpu-cache-environment.json').read_text());assert env['normal_jit']and env['software_tso']and env['initial_jit_bytes']==reserve*2**20
 raw=complete_rows(case/'frames.csv');end=float(raw[-1]['t_s']);frames=[f for f in raw if float(f['t_s'])>=end-30 and float(f['frame_ms'])>0];assert len(frames)>300
 frame_ms=[float(f['frame_ms'])for f in frames];assert sum(int(f['compiles'])for f in frames)==0,'late GPU compilation'
 camera=[statistics.median(float(f[k])for f in frames if f[k])for k in ('cam_x','cam_y','cam_z')]
 # Frame CSV uses time since first GPU frame; events use monotonic. Do not pretend exact action alignment.
 scene_start=next(float(f['t_s'])for f in raw if all(f[k]and abs(float(f[k])-v)<.001 for k,v in zip(('cam_x','cam_y','cam_z'),camera)))+3
 gameplay=[f for f in raw if float(f['t_s'])>=scene_start and float(f['frame_ms'])>0];assert len(gameplay)>len(frames)
 gm=[float(f['frame_ms'])for f in gameplay];duration=sum(gm)/1000
 samples=[s for s in r['samples']if s['seconds']>=r['seconds']-30];assert len(samples)>30
 roles={}
 for s in samples:
  for u in s['usage']:roles.setdefault((u['role'],u['pid']),[]).append((s['seconds'],u))
 cpu={}
 for(role,pid),values in roles.items():
  if len(values)<2:continue
  (at,a),(bt,b)=values[0],values[-1];delta=b['cpu_user_ns']+b['cpu_system_ns']-a['cpu_user_ns']-a['cpu_system_ns'];assert delta>=0 and bt>at
  cpu[role]=dict(pid=pid,percent_one_core=100*delta/1e9/(bt-at))
 assert 'bb-game-arm64'in cpu and 'bb-gpu'in cpu
 ready=next(x for x in events if x['event']=='character_ready')
 return dict(index=index,variant=variant,evidence=str(case),exit=r['exit'],stopped=r['stopped'],seconds=r['seconds'],ready_seconds=(ready['mono_ns']-r['samples'][0]['observation']['start_mono_ns'])/1e9,
  fps=1000/statistics.mean(frame_ms),p95_frame_ms=percentile(frame_ms,.95),p99_frame_ms=percentile(frame_ms,.99),max_frame_ms=max(frame_ms),one_percent_low_fps=1000/statistics.mean(sorted(frame_ms,reverse=True)[:max(1,math.ceil(len(frame_ms)*.01))]),gpu_ms=statistics.mean(float(f['gpu_ms'])for f in frames),late_gpu_compiles=0,
  combined_median_footprint_mib=statistics.median(sum(u['footprint_bytes']for u in s['usage'])/2**20 for s in samples),combined_median_rss_mib=statistics.median(sum(u['resident_bytes']for u in s['usage'])/2**20 for s in samples),cpu_usage=cpu,
  swap_change_mib=r['samples'][-1]['swap_mib']-r['samples'][0]['swap_mib'],pressure_counts={str(k):sum(s['pressure']==k for s in r['samples'])for k in (1,2,4)},camera=camera,cache_installed=installed,precompiled_dispatch_verified=dispatch,
  gameplay_window=dict(csv_start_s=scene_start,frame_count=len(gm),duration_s=duration,p99_ms=percentile(gm,.99),max_ms=max(gm),hitches_per_minute={str(t):60*sum(v>t for v in gm)/duration for t in (33.3,50,100)},gpu_compiles=sum(int(f['compiles'])for f in gameplay)),memory_policy=r['memory_policy'])

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cache',type=Path,required=True);ap.add_argument('--resume',type=Path);a=ap.parse_args();cache=a.cache.resolve();assert cache.is_file()and cache.is_relative_to(P/'evidence')
 e=a.resume.resolve()if a.resume else P/'evidence'/f'startup-cache-matrix-{time.time_ns()}'
 if a.resume:assert e.is_dir()and e.is_relative_to(P/'evidence')
 else:e.mkdir()
 artifacts=[P/'build/fex/Bin/bb-game-arm64',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib',P/'backend/bbport/bin/Bloodborne Metal.app/Contents/MacOS/bb-gpu',cache]
 def hashes():return {str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in artifacts}
 frozen=hashes();order=['both','cache','baseline','reserve','reserve','baseline','cache','both']
 report=dict(scope='private same-build 2x2 CPU disk-cache vs initial JIT virtual reserve; fixed cloned QA; same warm Metal cache, rendering/effects; zero-gain SDL, no capture/audits/profilers',previous_goal_turn='status-only, no progress; next safe action is this controlled matrix',artifact_sha256=frozen,compiled_build=json.loads((P/'build/fex/generated/cpu_cache_build.json').read_text()),order=order,runs=[],passed=False)
 if a.resume:
  old=json.loads((e/'result.json').read_text());assert old['artifact_sha256']==frozen and old['order']==order
  report=old;report.pop('error',None);report['resume_utc_ns']=time.time_ns()
 def save():(e/'result.json').write_text(json.dumps(report,indent=2)+'\n')
 save();print('Matrix evidence:',e,flush=True)
 try:
  for index,variant in enumerate(order):
   assert hashes()==frozen,'frozen artifact changed'
   if index<len(report['runs']):continue
   cmd=[sys.executable,str(P/'tools/run_stutter_case.py'),'--native-observer','--resource-hooks','--resource-fast-dispatch','--resource-leaf-dispatch','--extra-heaps','--cpu-cache-control','--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--seconds','90','--cycles','2']
   if variant in ('reserve','both'):cmd+=['--jit-reserve-mib','128']
   if variant in ('cache','both'):cmd+=['--cpu-cache',str(cache)]
   print('Starting',index,variant,flush=True)
   logfile=e/f'{index}-{variant}.log'
   if a.resume and logfile.exists():
    output=logfile.read_text();assert 'supervisor_exit 0'in output,'previous run was not complete; refuse automatic rerun'
   else:
    run=subprocess.run(cmd,capture_output=True,text=True,timeout=135);output=run.stdout+run.stderr;logfile.write_text(output);assert run.returncode==0,(index,variant,'incomplete action case',run.returncode)
   found=re.findall(r'^Evidence: (.+)$',output,re.M);assert len(found)==1
   row=summarize(Path(found[0]),variant,index)
   if report['runs']:assert all(abs(x-y)<.001 for x,y in zip(row['camera'],report['runs'][0]['camera'])),'camera differs'
   assert hashes()==frozen
   report['runs'].append(row);save();print(json.dumps({k:row[k]for k in ('index','variant','fps','ready_seconds','combined_median_footprint_mib','swap_change_mib','precompiled_dispatch_verified')}),flush=True)
  for v in ('baseline','reserve','cache','both'):
   rows=[r for r in report['runs']if r['variant']==v];report[v]={k:statistics.mean(r[k]for r in rows)for k in ('fps','ready_seconds','p95_frame_ms','p99_frame_ms','one_percent_low_fps','combined_median_footprint_mib','gpu_ms')};report[v]['individual_fps']=[r['fps']for r in rows]
  report['comparisons']={v:dict(fps_change_percent=100*(report[v]['fps']/report['baseline']['fps']-1),memory_delta_mib=report[v]['combined_median_footprint_mib']-report['baseline']['combined_median_footprint_mib'],ready_delta_s=report[v]['ready_seconds']-report['baseline']['ready_seconds'])for v in ('reserve','cache','both')}
  report['limitations']='Two repetitions each, fixed scene and repeated blood-bullet/heal only; no universal benefit, death/F3/network proof. Combined process footprint is accounting, not deduplicated physical RAM. Gameplay frame window is camera-established then continuous, not precisely aligned action events.';report['passed']=True;save();print('Completed:',e,flush=True)
 except BaseException as exc:
  report['error']=repr(exc);save();raise
if __name__=='__main__':main()
