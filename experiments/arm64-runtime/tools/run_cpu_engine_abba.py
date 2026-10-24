#!/usr/bin/env python3
"""Current native/FEX (published HLE lookup) versus Rosetta, identical ARM64 Metal host."""
from pathlib import Path
import argparse,hashlib,json,statistics,subprocess,sys,time,re
from run_hle_lookup_abba import complete_rows
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--extra-heaps',action='store_true',help='current explicit full lifecycle candidate on FEX only; Rosetta reference keeps original policy');ap.add_argument('--native-observer',action='store_true');ap.add_argument('--packed-veneers',action='store_true');ap.add_argument('--hot-staging-reuse',action='store_true',help='same Metal policy in BOTH engines');ap.add_argument('--seconds',type=int,default=90);options=ap.parse_args();assert 90<=options.seconds<=180
 e=P/'evidence'/f'cpu-engine-abba-{time.time_ns()}';e.mkdir()
 files=[P/'build/fex/Bin/bb-game-arm64',P/'rosetta-reference/bin/bb-probe-metal',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib',P/'backend/bbport/bin/Bloodborne Metal.app/Contents/MacOS/bb-gpu']
 hashes={str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in files}
 report=dict(scope='current ARM64/FEX published HLE lookup vs isolated Rosetta CPU; same ARM64 Metal GPU host, fixed cloned QA checkpoint, native 1080p, identical effects/Metal settings, zero-gain SDL, no audit/profiler/recording',artifact_sha256=hashes,observer='native SDK APIs'if options.native_observer else 'legacy commands',arm64_extra_heaps=options.extra_heaps,arm64_packed_veneers=options.packed_veneers,shared_hot_staging_reuse=options.hot_staging_reuse,seconds_per_run=options.seconds,passed=False,policy_difference='FEX adds original+filtered+leaf and actual-heap lifecycle hooks' if options.extra_heaps else 'original CPU policies',runs=[])
 (e/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('ABBA evidence:',e,flush=True)
 for index,engine in enumerate(('arm64','rosetta','rosetta','arm64')):
  assert hashes=={str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in files}
  cmd=[sys.executable,str(P/'tools/run_stutter_case.py'),'--engine',engine,'--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--seconds',str(options.seconds)]
  if options.packed_veneers and engine=='arm64':cmd+=['--packed-veneers']
  if options.native_observer:cmd+=['--native-observer']
  if options.hot_staging_reuse:cmd+=['--hot-staging-reuse']
  if options.extra_heaps and engine=='arm64':cmd+=['--resource-hooks','--resource-fast-dispatch','--resource-leaf-dispatch','--extra-heaps']
  run=subprocess.run(cmd,capture_output=True,text=True,timeout=options.seconds+40);(e/f'{index}-{engine}.log').write_text(run.stdout+run.stderr);assert run.returncode==0,(e,index)
  found=re.findall(r'^Evidence: (.+)$',run.stdout,re.M);assert len(found)==1;case=Path(found[0]);r=json.loads((case/'result.json').read_text());policy=json.loads((case/'cpu-policy.json').read_text());assert policy['fast_hle_lookup']==(engine=='arm64') and r['cpu_time_schema_version']==2
  conditions=json.loads((case/'stutter-case-conditions.json').read_text());text=(case/'runtime.log').read_text(errors='replace')
  assert policy['packed_veneers']==(options.packed_veneers and engine=='arm64')
  assert conditions['packed_veneers']==(options.packed_veneers and engine=='arm64')
  assert conditions['hot_staging_reuse']==options.hot_staging_reuse
  gpu_policy=json.loads((case/'gpu-experimental-policy.json').read_text());assert gpu_policy['hot_staging_reuse']==options.hot_staging_reuse
  assert bool(text.count('Metal staging recent reuse: enabled;'))==options.hot_staging_reuse
  assert not conditions['staging_audit']and not conditions['native_frame_capture']
  startup_hash=hashlib.file_digest((case/'startup.bin').open('rb'),'sha256').hexdigest();launcher=json.loads((case/'launcher-environment.json').read_text())
  if report['runs']:assert startup_hash==report['runs'][0]['startup_sha256']and launcher==report['runs'][0]['launcher_environment'],'startup or Metal environment differs'
  if engine=='arm64':
   layout='darwin-packed-guest-ro-v1' if options.packed_veneers else 'standalone-native-rx-v1'
   assert text.count('ARM VENEER layout='+layout+' ')==1,'actual veneer layout differs'
  assert conditions['native_observer']==options.native_observer and not conditions.get('guest_frame_audit',False)and not conditions.get('guest_sync_audit',False)
  assert not conditions['audit'] and not conditions['sample_cpu'] and not conditions['compile_audit'] and not conditions['extra_heaps_fresh']
  assert conditions['extra_heaps']==(options.extra_heaps and engine=='arm64')
  assert 'character ready; automatic menu input released'in text and 'ARM FATAL'not in text and 'ARM RESOURCE invalid metadata'not in text
  assert text.count('benchmark stream muted; original audio backend and cadence retained')>=2
  assert text.count('ARM EXTRA 5/5 actual heap hooks installed')==int(options.extra_heaps and engine=='arm64')
  raw=complete_rows(case/'frames.csv');end=float(raw[-1]['t_s']);frames=[f for f in raw if float(f['t_s'])>=end-30 and float(f['frame_ms'])>0];samples=[s for s in r['samples'] if s['seconds']>=r['seconds']-30];roles={}
  for s in samples:
   for u in s['usage']:roles.setdefault((u['role'],u['pid']),[]).append((s['seconds'],u))
  cpu={}
  for(role,pid),values in roles.items():
   if len(values)<2:continue
   (at,a),(bt,b)=values[0],values[-1];delta=b['cpu_user_ns']+b['cpu_system_ns']-a['cpu_user_ns']-a['cpu_system_ns'];cpu[role]=dict(pid=pid,percent_one_core=100*delta/1e9/(bt-at),cpu_seconds=delta/1e9,interval_s=bt-at)
  row=dict(index=index,engine=engine,startup_sha256=startup_hash,launcher_environment=launcher,draws_median=statistics.median(float(f['draws'])for f in frames),evidence=str(case),exit=r['exit'],stopped=r['stopped'],seconds=r['seconds'],fps=1000/statistics.mean(float(f['frame_ms'])for f in frames),p95_frame_ms=sorted(float(f['frame_ms'])for f in frames)[int((len(frames)-1)*.95)],p99_frame_ms=sorted(float(f['frame_ms'])for f in frames)[int((len(frames)-1)*.99)],max_frame_ms=max(float(f['frame_ms'])for f in frames),gpu_ms=statistics.mean(float(f['gpu_ms'])for f in frames),combined_median_footprint_mib=statistics.median(sum(u['footprint_bytes']for u in s['usage'])/2**20 for s in samples),cpu_usage=cpu,swap_change_mib=r['samples'][-1]['swap_mib']-r['samples'][0]['swap_mib'],pressure_max=max(s['pressure']for s in r['samples']),late_compiles=sum(int(f['compiles'])for f in frames),camera=sorted({(f['cam_x'],f['cam_y'],f['cam_z'])for f in frames if f['cam_x']}))
  assert row['exit']==0 and row['stopped']=='time budget'and not r['cleanup_warnings']and row['late_compiles']==0
  report['runs'].append(row);(e/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in row.items()if k!='launcher_environment'}),flush=True)
 assert len({tuple(map(tuple,r['camera']))for r in report['runs']})==1,'camera differs'
 for engine in ('arm64','rosetta'):
  rows=[r for r in report['runs']if r['engine']==engine];role='bb-game-arm64' if engine=='arm64' else 'bb-probe-metal';report[engine]=dict(mean_fps=statistics.mean(r['fps']for r in rows),mean_median_combined_footprint_mib=statistics.mean(r['combined_median_footprint_mib']for r in rows),mean_game_cpu_percent=statistics.mean(r['cpu_usage'][role]['percent_one_core']for r in rows))
 report['native_fps_change_percent']=100*(report['arm64']['mean_fps']/report['rosetta']['mean_fps']-1);report['native_memory_delta_mib']=report['arm64']['mean_median_combined_footprint_mib']-report['rosetta']['mean_median_combined_footprint_mib'];report['warning']='One fixed scene, short ABBA only; CPU percent is aggregate process work; combined footprint is not deduplicated RAM. Death/respawn and authenticated online remain separate gates.'
 report['passed']=True
 (e/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('Evidence:',e,flush=True)
if __name__=='__main__':main()
