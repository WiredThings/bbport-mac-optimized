#!/usr/bin/env python3
"""Same-build no-diagnostic off/on/on/off staging performance comparison."""
from pathlib import Path
import hashlib,json,re,statistics,subprocess,sys,time
from run_hle_lookup_abba import complete_rows
P=Path(__file__).resolve().parents[1]
def main():
 e=P/'evidence'/f'hot-staging-abba-{time.time_ns()}';e.mkdir();files=[P/'build/fex/Bin/bb-game-arm64',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib']
 def hashes():return {str(f):hashlib.file_digest(f.open('rb'),'sha256').hexdigest()for f in files}
 frozen=hashes();r=dict(scope='same ARM64/FEX/Metal binary and complete lifecycle hooks, fixed QA/graphics/camera/SDL zero gain, no native capture/audit/profiler/visual service; last30s after original idle threshold',artifact_sha256=frozen,runs=[],passed=False)
 def save():(e/'result.json').write_text(json.dumps(r,indent=2)+'\n')
 save();print('ABBA evidence:',e,flush=True)
 for index,hot in enumerate((False,True,True,False)):
  assert hashes()==frozen
  cmd=[sys.executable,str(P/'tools/run_stutter_case.py'),'--native-observer','--packed-veneers','--resource-hooks','--resource-fast-dispatch','--resource-leaf-dispatch','--extra-heaps','--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--seconds','120']
  if hot:cmd+=['--hot-staging-reuse']
  x=subprocess.run(cmd,capture_output=True,text=True,timeout=160);(e/f'{index}-{hot}.log').write_text(x.stdout+x.stderr);assert x.returncode==0,'incomplete run; inspect existing owner, never restart blindly'
  found=re.findall(r'^Evidence: (.+)$',x.stdout,re.M);assert len(found)==1;case=Path(found[0]);q=json.loads((case/'result.json').read_text());c=json.loads((case/'stutter-case-conditions.json').read_text());text=(case/'runtime.log').read_text(errors='replace')
  assert q['exit']==0 and q['stopped']=='time budget'and not q['cleanup_warnings']and q['cpu_time_schema_version']==2
  assert c['hot_staging_reuse']==hot and c['packed_veneers']and c['extra_heaps']and not c['staging_audit']and not c['native_frame_capture']and not c['audit']and not c['sample_cpu']and not c['compile_audit']
  assert bool(text.count('Metal staging recent reuse: enabled;'))==hot and 'character ready; automatic menu input released'in text and 'ARM RESOURCE invalid metadata'not in text and 'ARM FATAL'not in text
  rows=complete_rows(case/'frames.csv');end=float(rows[-1]['t_s']);tail=[f for f in rows if float(f['t_s'])>=end-30 and float(f['frame_ms'])>0];samples=[s for s in q['samples']if s['seconds']>=q['seconds']-30]
  values=sorted(float(f['frame_ms'])for f in tail)
  def percentile(p):return values[min(len(values)-1,int((len(values)-1)*p))]
  role={}
  for s in samples:
   for u in s['usage']:role.setdefault((u['role'],u['pid']),[]).append((s['seconds'],u))
  cpu={}
  for(key,pid),v in role.items():
   if len(v)<2:continue
   (at,a),(bt,b)=v[0],v[-1];cpu[key]=100*(b['cpu_user_ns']+b['cpu_system_ns']-a['cpu_user_ns']-a['cpu_system_ns'])/1e9/(bt-at)
  row=dict(index=index,hot=hot,case=str(case),fps=1000/statistics.mean(values),p95_ms=percentile(.95),p99_ms=percentile(.99),max_ms=max(values),gpu_ms=statistics.mean(float(f['gpu_ms'])for f in tail),median_combined_footprint_mib=statistics.median(sum(u['footprint_bytes']for u in s['usage'])/2**20 for s in samples),cpu_percent=cpu,camera=[statistics.median(float(f[k])for f in tail if f[k])for k in ['cam_x','cam_y','cam_z']],draws_median=statistics.median(float(f['draws'])for f in tail),late_compiles=sum(int(f['compiles'])for f in tail),pressure_max=max(s['pressure']for s in q['samples']),swap_change_mib=q['samples'][-1]['swap_mib']-q['samples'][0]['swap_mib'],startup_sha256=hashlib.file_digest((case/'startup.bin').open('rb'),'sha256').hexdigest(),launcher_environment=json.loads((case/'launcher-environment.json').read_text()))
  assert row['late_compiles']==0
  if r['runs']:assert row['camera']==r['runs'][0]['camera']and row['startup_sha256']==r['runs'][0]['startup_sha256']and row['launcher_environment']==r['runs'][0]['launcher_environment']
  r['runs'].append(row);assert hashes()==frozen;save();print({k:v for k,v in row.items()if k not in ['launcher_environment','startup_sha256']},flush=True)
 for flag,name in [(False,'baseline'),(True,'hot')]:
  xs=[x for x in r['runs']if x['hot']==flag];r[name]={key:statistics.mean(x[key]for x in xs)for key in ['fps','p95_ms','p99_ms','gpu_ms','median_combined_footprint_mib']}
 r['fps_change_percent']=100*(r['hot']['fps']/r['baseline']['fps']-1);r['process_accounting_delta_mib']=r['hot']['median_combined_footprint_mib']-r['baseline']['median_combined_footprint_mib'];r['passed']=True;r['limitations']='One short ABBA; combined process footprint is not shared-deduplicated RAM, low-frame randomness remains. No death/account verification from this cohort.';save();print('ABBA complete:',e,flush=True)
if __name__=='__main__':main()
