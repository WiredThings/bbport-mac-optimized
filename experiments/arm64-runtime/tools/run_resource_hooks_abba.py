#!/usr/bin/env python3
"""Unprofiled fixed-scene A-B-B-A for the optional FEX resource lifecycle detours, isolated only."""
from pathlib import Path
import subprocess,sys,time,json,csv,statistics,re,hashlib
P=Path(__file__).resolve().parents[1]
def complete_rows(path):
 rows=[]
 for row in csv.DictReader(path.open()):
  if None in row or any(v is None for v in row.values()):continue
  try:float(row['t_s']);float(row['frame_ms']);float(row['gpu_ms'])
  except (ValueError,TypeError):continue
  rows.append(row)
 return rows
def main():
 artifacts=[P/'build/fex/Bin/bb-game-arm64',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib']
 hashes={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in artifacts}
 e=P/'evidence'/f'resource-hooks-abba-{time.time_ns()}';e.mkdir();report=dict(scope='same ARM64 CPU binary + ARM64 GPU; only FEX resource hooks + authoritative lifetime leases differ; both use published HLE lookup; fixed cloned QA, native 1080p, same launcher effects/Metal settings, zero-gain SDL, no audit/profiler/screenshots/recording; one ABBA short group',runs=[],artifact_sha256=hashes)
 for index,enabled in enumerate((False,True,True,False)):
  assert hashes=={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in artifacts},'runtime changed during ABBA'
  cmd=[sys.executable,str(P/'tools/run_stutter_case.py'),'--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--seconds','90']
  cmd+=['--resource-hooks'] if enabled else []
  run=subprocess.run(cmd,capture_output=True,text=True,timeout=130);(e/f'{index}-{"enabled" if enabled else "disabled"}.log').write_text(run.stdout+run.stderr)
  assert run.returncode==0,(e,index,run.returncode)
  found=re.findall(r'^Evidence: (.+)$',run.stdout,re.M);assert len(found)==1;case=Path(found[0]);r=json.loads((case/'result.json').read_text());conditions=json.loads((case/'stutter-case-conditions.json').read_text());assert conditions['fast_hle_lookup'] and not conditions['audit'] and not conditions['sample_cpu']
  raw=complete_rows(case/'frames.csv');end=float(raw[-1]['t_s']);frames=[f for f in raw if float(f['t_s'])>=end-30 and float(f['frame_ms'])>0];samples=[s for s in r['samples'] if s['seconds']>=r['seconds']-30];mem=[sum(x['footprint_bytes'] for x in s['usage'])/2**20 for s in samples]
  row=dict(index=index,enabled=enabled,evidence=str(case),exit=r['exit'],stopped=r['stopped'],seconds=r['seconds'],fps=1000/statistics.mean(float(f['frame_ms']) for f in frames),max_frame_ms=max(float(f['frame_ms']) for f in frames),gpu_ms=statistics.mean(float(f['gpu_ms']) for f in frames),combined_median_footprint_mib=statistics.median(mem),swap_change_mib=r['samples'][-1]['swap_mib']-r['samples'][0]['swap_mib'],pressure_max=max(s['pressure'] for s in r['samples']),late_compiles=sum(int(f['compiles']) for f in frames),camera=sorted({(f['cam_x'],f['cam_y'],f['cam_z']) for f in frames if f['cam_x']}))
  text=(case/'runtime.log').read_text(errors='replace');row['hook_installs']=text.count('ARM RESOURCE 10/10 startup FEX detours');row['metadata_rejections']=text.count('ARM RESOURCE invalid metadata');row['stale_fault_retry_log_records']=text.count('ARM GPU FAULT stale watch already lifted');row['character_ready']='character ready; automatic menu input released' in text
  assert row['hook_installs']==int(enabled) and row['metadata_rejections']==0 and row['character_ready']
  assert row['exit']==0 and row['stopped']=='time budget' and row['late_compiles']==0
  report['runs'].append(row);(e/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(row),flush=True)
 for enabled,label in ((False,'disabled'),(True,'enabled')):
  rows=[r for r in report['runs'] if r['enabled']==enabled];report[label]=dict(mean_fps=statistics.mean(r['fps'] for r in rows),mean_median_footprint_mib=statistics.mean(r['combined_median_footprint_mib'] for r in rows),mean_gpu_ms=statistics.mean(r['gpu_ms'] for r in rows))
 report['fps_change_percent']=100*(report['enabled']['mean_fps']/report['disabled']['mean_fps']-1)
 report['memory_change_mib']=report['enabled']['mean_median_footprint_mib']-report['disabled']['mean_median_footprint_mib']
 assert len({tuple(map(tuple,r['camera'])) for r in report['runs']})==1,'camera differs across cases'
 report['warning']='Only one short ABBA, not universal performance or lifetime validation; combined footprint is accounting, not deduplicated RAM. F3/death/server-login are separate gates.'
 (e/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('Evidence:',e,flush=True)
if __name__=='__main__':main()
