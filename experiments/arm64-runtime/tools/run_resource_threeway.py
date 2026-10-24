#!/usr/bin/env python3
"""Same-binary A-B-C-C-B-A; all Shared QA, no profiler or recording."""
from pathlib import Path
import csv,hashlib,json,re,statistics,subprocess,sys,time
P=Path(__file__).resolve().parents[1]
def main():
 artifacts=[P/'build/fex/Bin/bb-game-arm64',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib']
 digest=lambda:{str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in artifacts}
 hashes=digest();e=P/'evidence'/f'resource-threeway-{time.time_ns()}';e.mkdir()
 report=dict(scope='same ARM64/FEX + ARM64 Metal binary, fixed private checkpoint and camera, native 1080p and unchanged launcher effects, zero-gain SDL, no audit/profiler/screenshots/recording; A=no new hooks, B=full legacy dispatch, C=full filtered+leaf dispatch',runs=[],artifact_sha256=hashes)
 modes=['no_hooks','legacy','leaf','leaf','legacy','no_hooks']
 for index,mode in enumerate(modes):
  assert digest()==hashes,'runtime changed during experiment'
  cmd=[sys.executable,str(P/'tools/run_stutter_case.py'),'--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--seconds','90']
  if mode!='no_hooks':cmd+=['--resource-hooks']
  if mode=='leaf':cmd+=['--resource-fast-dispatch','--resource-leaf-dispatch']
  run=subprocess.run(cmd,capture_output=True,text=True,timeout=130);(e/f'{index}-{mode}.log').write_text(run.stdout+run.stderr)
  assert run.returncode==0,(mode,e,run.returncode)
  found=re.findall(r'^Evidence: (.+)$',run.stdout,re.M);assert len(found)==1;case=Path(found[0]);r=json.loads((case/'result.json').read_text());conditions=json.loads((case/'stutter-case-conditions.json').read_text());text=(case/'runtime.log').read_text(errors='replace')
  assert conditions['resource_hooks']==(mode!='no_hooks') and conditions['resource_fast_dispatch']==(mode=='leaf') and conditions['resource_leaf_dispatch']==(mode=='leaf')
  assert conditions['fast_hle_lookup']and not conditions['audit']and not conditions['sample_cpu']
  assert r['cpu_time_schema_version']==2 and r['exit']==0 and r['stopped']=='time budget'and not r['cleanup_warnings']
  frames=[]
  for row in csv.DictReader((case/'frames.csv').open()):
   if None in row or any(v is None for v in row.values()):continue
   try:float(row['t_s']);float(row['frame_ms']);float(row['gpu_ms'])
   except (ValueError,TypeError):continue
   frames.append(row)
  end=float(frames[-1]['t_s']);frames=[f for f in frames if float(f['t_s'])>=end-30 and float(f['frame_ms'])>0]
  samples=[s for s in r['samples']if s['seconds']>=r['seconds']-30];memory=[sum(u['footprint_bytes']for u in s['usage'])/2**20 for s in samples];cpu={};footprint={}
  for role in ['bb-game-arm64','bb-gpu']:
   points=[(s['seconds'],u)for s in samples for u in s['usage']if u['role']==role]
   assert len(points)>20 and len({u['pid']for _,u in points})==1
   a,b=points[0],points[-1];cpu[role]=100*((b[1]['cpu_user_ns']+b[1]['cpu_system_ns'])-(a[1]['cpu_user_ns']+a[1]['cpu_system_ns']))/1e9/(b[0]-a[0]);footprint[role]=statistics.median(u['footprint_bytes']/2**20 for _,u in points)
  row=dict(index=index,mode=mode,evidence=str(case),exit=r['exit'],stopped=r['stopped'],seconds=r['seconds'],fps=1000/statistics.mean(float(f['frame_ms'])for f in frames),gpu_ms=statistics.mean(float(f['gpu_ms'])for f in frames),max_frame_ms=max(float(f['frame_ms'])for f in frames),combined_median_footprint_mib=statistics.median(memory),role_median_footprint_mib=footprint,cpu_percent_one_core=cpu,pressure_max=max(s['pressure']for s in r['samples']),swap_change_mib=r['samples'][-1]['swap_mib']-r['samples'][0]['swap_mib'],late_compiles=sum(int(f['compiles'])for f in frames),camera=sorted({(f['cam_x'],f['cam_y'],f['cam_z'])for f in frames if f['cam_x']}))
  assert 'character ready; automatic menu input released'in text and row['late_compiles']==0
  assert text.count('ARM RESOURCE 10/10 startup FEX detours')==int(mode!='no_hooks')and text.count('ARM RESOURCE dispatch-filter=1')==int(mode=='leaf')and text.count('ARM RESOURCE leaf-dispatch=1')==int(mode=='leaf')
  assert 'ARM RESOURCE invalid metadata'not in text and 'ARM FATAL'not in text and 'ARM GPU FAULT unowned'not in text and 'ARM GPU FAULT rejected'not in text
  report['runs'].append(row);(e/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(row),flush=True)
 assert len({tuple(map(tuple,r['camera']))for r in report['runs']})==1,'camera differs'
 for mode in ['no_hooks','legacy','leaf']:
  rows=[r for r in report['runs']if r['mode']==mode];report[mode]=dict(mean_fps=statistics.mean(r['fps']for r in rows),mean_gpu_ms=statistics.mean(r['gpu_ms']for r in rows),mean_median_combined_footprint_mib=statistics.mean(r['combined_median_footprint_mib']for r in rows),mean_game_cpu_percent=statistics.mean(r['cpu_percent_one_core']['bb-game-arm64']for r in rows))
 report['leaf_vs_legacy_fps_percent']=100*(report['leaf']['mean_fps']/report['legacy']['mean_fps']-1)
 report['leaf_vs_no_hooks_fps_percent']=100*(report['leaf']['mean_fps']/report['no_hooks']['mean_fps']-1)
 report['leaf_vs_no_hooks_memory_mib']=report['leaf']['mean_median_combined_footprint_mib']-report['no_hooks']['mean_median_combined_footprint_mib']
 report['warning']='One fixed-scene short six-run group. CPU percent sums cores, not frame latency. Combined footprint is accounting, not deduplicated physical RAM; no rare-low or long-run claim. Death/server-login/unload remain separate gates.'
 (e/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('Evidence:',e,flush=True)
if __name__=='__main__':main()
