#!/usr/bin/env python3
"""Same binary, only guest-veneer layout differs; private, silent fixed-scene ABBA."""
from pathlib import Path
import hashlib,json,math,re,statistics,subprocess,sys,time
from run_hle_lookup_abba import complete_rows
from run_startup_cache_matrix import percentile
P=Path(__file__).resolve().parents[1]
def metrics(case,packed,index):
 r=json.loads((case/'result.json').read_text());c=json.loads((case/'stutter-case-conditions.json').read_text());policy=json.loads((case/'cpu-policy.json').read_text());log=(case/'runtime.log').read_text(errors='replace')
 assert r['exit']==0 and r['stopped']=='time budget'and not r['cleanup_warnings']and not r['generations']
 assert r['cpu_time_schema_version']==2 and r['observer']=='native SDK APIs'
 assert c['packed_veneers']==packed and policy['packed_veneers']==packed and policy['fast_hle_lookup']and policy['software_tso_not_bypassed']
 assert not any(c[k]for k in ('audit','sample_cpu','compile_audit','cpu_code_map','fex_mapping_audit','guest_frame_audit','guest_sync_audit','pipeline_archive','extra_heaps_fresh','cpu_cache_control'))and c['cpu_cache']is None and c['jit_initial_reserve_mib']==16
 assert c['cycles']==2 and c['extra_heaps']and c['native_observer']
 assert log.count('ARM EXTRA 5/5 actual heap hooks installed')==1 and log.count('benchmark stream muted; original audio backend and cadence retained')>=2
 assert 'ARM FATAL'not in log and'ARM RESOURCE invalid metadata'not in log and'BACHATA_FEX_MAPPING_FAIL'not in log
 mode='darwin-packed-guest-ro-v1'if packed else'standalone-native-rx-v1';marker=re.findall(r'ARM VENEER layout=(\S+) guest_pages=(\d+) guest_virtual_bytes=(\d+);',log);assert len(marker)==1 and marker[0][0]==mode
 pages=int(marker[0][1]);assert(pages==1 if packed else pages>=350)
 initial=log[:log.index('ARM VENEER layout=')];addresses=[int(x,16)for x in re.findall(r'BACHATA_FEX_VENEER address=(0x[0-9a-f]+)',initial)];assert len(addresses)>=350 and len(set(addresses))==len(addresses)
 events=[json.loads(x)for x in(case/'operator-events.jsonl').read_text().splitlines()];assert sum(x['event']=='character_ready'for x in events)==1 and sum(x['event']=='cycle_end'for x in events)==2
 raw=complete_rows(case/'frames.csv');end=float(raw[-1]['t_s']);frames=[f for f in raw if float(f['t_s'])>=end-30 and float(f['frame_ms'])>0];assert len(frames)>300 and sum(int(f['compiles'])for f in frames)==0
 camera=[statistics.median(float(f[k])for f in frames if f[k])for k in ('cam_x','cam_y','cam_z')];fm=[float(f['frame_ms'])for f in frames];fps=1000/statistics.mean(fm)
 scene=next(float(f['t_s'])for f in raw if all(f[k]and abs(float(f[k])-v)<.001 for k,v in zip(('cam_x','cam_y','cam_z'),camera)))+3
 gameplay=[f for f in raw if float(f['t_s'])>=scene and float(f['frame_ms'])>0];gm=[float(f['frame_ms'])for f in gameplay];duration=sum(gm)/1000
 samples=[s for s in r['samples']if s['seconds']>=r['seconds']-30];assert len(samples)>30;roles={}
 for sample in samples:
  for u in sample['usage']:roles.setdefault((u['role'],u['pid']),[]).append((sample['seconds'],u))
 cpu={}
 for(role,pid),v in roles.items():
  if len(v)<2:continue
  (at,a),(bt,b)=v[0],v[-1];delta=b['cpu_user_ns']+b['cpu_system_ns']-a['cpu_user_ns']-a['cpu_system_ns'];assert delta>=0 and bt>at;cpu[role]=dict(pid=pid,percent_one_core=100*delta/1e9/(bt-at))
 assert 'bb-game-arm64'in cpu and'bb-gpu'in cpu
 return dict(index=index,packed=packed,evidence=str(case),fps=fps,p95_frame_ms=percentile(fm,.95),p99_frame_ms=percentile(fm,.99),one_percent_low_fps=1000/statistics.mean(sorted(fm,reverse=True)[:max(1,math.ceil(len(fm)*.01))]),gpu_ms=statistics.mean(float(f['gpu_ms'])for f in frames),max_frame_ms=max(fm),cpu=cpu,game_cpu_ms_per_frame=10*cpu['bb-game-arm64']['percent_one_core']/fps,
  combined_median_footprint_mib=statistics.median(sum(u['footprint_bytes']for u in s['usage'])/2**20 for s in samples),camera=camera,swap_change_mib=r['samples'][-1]['swap_mib']-r['samples'][0]['swap_mib'],pressure_counts={str(k):sum(s['pressure']==k for s in r['samples'])for k in (1,2,4)},initial_guest_pages=pages,initial_veneer_count=len(addresses),initial_min_l1_indices=len({a&8191 for a in addresses}),initial_max_l1_indices=len({a&1048575 for a in addresses}),gameplay_window=dict(csv_start_s=scene,duration_s=duration,p99_ms=percentile(gm,.99),max_ms=max(gm),hitches_per_minute={str(t):60*sum(v>t for v in gm)/duration for t in (33.3,50,100)}))

def main():
 e=P/'evidence'/f'veneer-layout-abba-{time.time_ns()}';e.mkdir();files=[P/'build/fex/Bin/bb-game-arm64',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib',P/'backend/bbport/bin/Bloodborne Metal.app/Contents/MacOS/bb-gpu']
 def hashes():return {str(f):hashlib.file_digest(f.open('rb'),'sha256').hexdigest()for f in files}
 frozen=hashes();report=dict(scope='same frozen CPU/GPU; only packed guest-veneer flag differs; normal16MiB initial JIT/no CPU disk cache, fixed cloned QA, identical warmGPU/rendering/effects/audio zero-gain; no profiling/audit/visual capture',artifact_sha256=frozen,runs=[],passed=False)
 def save():(e/'result.json').write_text(json.dumps(report,indent=2)+'\n')
 save();print('ABBA evidence:',e,flush=True)
 try:
  for index,packed in enumerate((False,True,True,False)):
   assert hashes()==frozen;cmd=[sys.executable,str(P/'tools/run_stutter_case.py'),'--native-observer','--resource-hooks','--resource-fast-dispatch','--resource-leaf-dispatch','--extra-heaps','--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--seconds','90','--cycles','2']
   if packed:cmd+=['--packed-veneers']
   print('Starting',index,'packed',packed,flush=True)
   run=subprocess.run(cmd,capture_output=True,text=True,timeout=135);(e/f'{index}-{"packed"if packed else"legacy"}.log').write_text(run.stdout+run.stderr);assert run.returncode==0,(index,'incomplete run',run.returncode)
   found=re.findall(r'^Evidence: (.+)$',run.stdout,re.M);assert len(found)==1;case=Path(found[0]);row=metrics(case,packed,index)
   if report['runs']:assert all(abs(x-y)<.001 for x,y in zip(row['camera'],report['runs'][0]['camera'])),'camera differs'
   assert hashes()==frozen;report['runs'].append(row);save();print(json.dumps({k:row[k]for k in ('index','packed','fps','game_cpu_ms_per_frame','combined_median_footprint_mib','initial_guest_pages','initial_min_l1_indices','swap_change_mib')}),flush=True)
  for packed,label in ((False,'legacy'),(True,'packed')):
   rows=[r for r in report['runs']if r['packed']==packed];report[label]={k:statistics.mean(r[k]for r in rows)for k in ('fps','p95_frame_ms','p99_frame_ms','one_percent_low_fps','gpu_ms','game_cpu_ms_per_frame','combined_median_footprint_mib')};report[label]['individual_fps']=[r['fps']for r in rows]
  report['comparison']=dict(fps_change_percent=100*(report['packed']['fps']/report['legacy']['fps']-1),game_cpu_per_frame_change_percent=100*(report['packed']['game_cpu_ms_per_frame']/report['legacy']['game_cpu_ms_per_frame']-1),memory_accounting_delta_mib=report['packed']['combined_median_footprint_mib']-report['legacy']['combined_median_footprint_mib']);report['limitations']='One short ABBA; no universal speed/no-stutter guarantee, deduplicated RAM, exact action-event frame alignment, enemy-death/account-network proof; index projection is not actual miss counting.';report['passed']=True;save();print('Completed:',e,flush=True)
 except BaseException as exc:report['error']=repr(exc);save();raise
if __name__=='__main__':main()
