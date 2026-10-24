#!/usr/bin/env python3
"""Fresh baseline/candidate/candidate/baseline, all in cloned private QA."""
from pathlib import Path
import argparse,hashlib,json,re,statistics,subprocess,sys,time
from run_startup_cache_matrix import summarize
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cache',type=Path,required=True);a=ap.parse_args();cache=a.cache.resolve();assert cache.is_file()and cache.is_relative_to(P/'evidence')
 e=P/'evidence'/f'cpu-cache-candidate-abba-{time.time_ns()}';e.mkdir()
 artifacts=[P/'build/fex/Bin/bb-game-arm64',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib',P/'backend/bbport/bin/Bloodborne Metal.app/Contents/MacOS/bb-gpu',cache]
 def hashes():return {str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in artifacts}
 frozen=hashes();report=dict(scope='fresh baseline16/no-cache vs first-observed CPU cache+128MiB initial virtual reserve; same frozen CPU/GPU, fixed cloned private QA, rendering/effects/warmGPU/audio unchanged; no audit/profile/recording',artifact_sha256=frozen,cache=str(cache),order=['baseline','both','both','baseline'],runs=[],passed=False)
 def save():(e/'result.json').write_text(json.dumps(report,indent=2)+'\n')
 save();print('ABBA evidence:',e,flush=True)
 try:
  for index,variant in enumerate(report['order']):
   assert hashes()==frozen
   cmd=[sys.executable,str(P/'tools/run_stutter_case.py'),'--native-observer','--resource-hooks','--resource-fast-dispatch','--resource-leaf-dispatch','--extra-heaps','--cpu-cache-control','--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--seconds','90','--cycles','2']
   if variant=='both':cmd+=['--jit-reserve-mib','128','--cpu-cache',str(cache)]
   print('Starting',index,variant,flush=True)
   run=subprocess.run(cmd,capture_output=True,text=True,timeout=135);(e/f'{index}-{variant}.log').write_text(run.stdout+run.stderr);assert run.returncode==0,(index,'incomplete run',run.returncode)
   found=re.findall(r'^Evidence: (.+)$',run.stdout,re.M);assert len(found)==1;case=Path(found[0]);conditions=json.loads((case/'stutter-case-conditions.json').read_text());assert conditions['cpu_cache']==(str(cache)if variant=='both'else None)
   row=summarize(case,variant,index)
   if report['runs']:assert all(abs(x-y)<.001 for x,y in zip(row['camera'],report['runs'][0]['camera'])),'camera differs'
   assert hashes()==frozen
   report['runs'].append(row);save();print(json.dumps({k:row[k]for k in ('index','variant','fps','ready_seconds','combined_median_footprint_mib','swap_change_mib','precompiled_dispatch_verified')}),flush=True)
  for variant in ('baseline','both'):
   rows=[r for r in report['runs']if r['variant']==variant];report[variant]={k:statistics.mean(r[k]for r in rows)for k in ('fps','ready_seconds','p95_frame_ms','p99_frame_ms','one_percent_low_fps','combined_median_footprint_mib','gpu_ms')};report[variant]['individual_fps']=[r['fps']for r in rows]
  report['comparison']=dict(fps_change_percent=100*(report['both']['fps']/report['baseline']['fps']-1),memory_delta_mib=report['both']['combined_median_footprint_mib']-report['baseline']['combined_median_footprint_mib'],ready_delta_s=report['both']['ready_seconds']-report['baseline']['ready_seconds']);report['passed']=True;report['limitations']='One short ABBA; no per-component attribution, deduplicated RAM, exact event-aligned hitch timing, new-scene/death/F3/authenticated networking proof.';save();print('Completed:',e,flush=True)
 except BaseException as exc:report['error']=repr(exc);save();raise
if __name__=='__main__':main()
