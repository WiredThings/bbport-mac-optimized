#!/usr/bin/env python3
"""Frozen private FEX CPU baseline versus PMR fix, restore current binary atomically."""
from pathlib import Path
import hashlib,subprocess,json,statistics,csv,sys,time,shutil,os,re
P=Path(__file__).resolve().parents[1];cpu=P/'build/fex/Bin/bb-game-arm64';gpu=P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib';baseline=P/'evidence/before-pmr-reclaim-1792799266623464000/bb-game-arm64'
def sha(p):return hashlib.file_digest(p.open('rb'),'sha256').hexdigest()
def idle():
 lines=subprocess.check_output(['/bin/ps','-axo','comm'],text=True).splitlines()
 assert not any(l.strip().endswith(('/bb-game-arm64','/bb-probe-metal','/bb-gpu'))for l in lines),'another game is live'
def install(source):
 idle();temp=cpu.with_name('.bb-game-pmr-abba-stage');assert not temp.exists();shutil.copy2(source,temp);os.replace(temp,cpu)
def main():
 idle();e=P/'evidence'/f'pmr-abba-{time.time_ns()}';e.mkdir();fixed=e/'bb-game-arm64-fixed';shutil.copy2(cpu,fixed);hashes=dict(baseline=sha(baseline),fixed=sha(fixed),gpu=sha(gpu));assert hashes['baseline']!=hashes['fixed'];report=dict(scope='private FEX CPU only PMR lifecycle baseline/fix ABBA, same QA snapshot/settings/Metal/cache budgets, all diagnostics off, muted, native observer',artifact_sha256=hashes,runs=[],restored=False);print('Evidence:',e,flush=True)
 try:
  for i,mode in enumerate(['baseline','fixed','fixed','baseline']):
   assert sha(gpu)==hashes['gpu']and sha(cpu)in [hashes['baseline'],hashes['fixed']],'unexpected external rebuild'
   install(baseline if mode=='baseline'else fixed);assert sha(cpu)==hashes[mode]
   cmd=[sys.executable,'tools/run_stutter_case.py','--resource-hooks','--resource-fast-dispatch','--resource-leaf-dispatch','--extra-heaps','--native-observer','--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--seconds','90','--cycles','2']
   out=subprocess.run(cmd,capture_output=True,text=True,timeout=135);(e/f'{i}-{mode}.log').write_text(out.stdout+out.stderr);assert out.returncode==0,'incomplete game run; see per-run log'
   paths=re.findall(r'^Evidence: (.+)$',out.stdout,re.M);assert len(paths)==1;c=Path(paths[0]);r=json.loads((c/'result.json').read_text());assert r['exit']==0 and r['stopped']=='time budget'and not r['cleanup_warnings'];cond=json.loads((c/'stutter-case-conditions.json').read_text());assert not any(cond.get(k,False)for k in ['audit','guest_frame_audit','guest_sync_audit','fex_mapping_audit','sample_cpu','compile_audit'])
   f=list(csv.DictReader((c/'frames.csv').open()));end=float(f[-1]['t_s']);f=[v for v in f if float(v['t_s'])>=end-30 and float(v['frame_ms'])>0];samples=[v for v in r['samples']if v['seconds']>=r['seconds']-30];row=dict(mode=mode,evidence=str(c),fps=1000/statistics.mean(float(v['frame_ms'])for v in f),gpu_ms=statistics.mean(float(v['gpu_ms'])for v in f),max_frame_ms=max(float(v['frame_ms'])for v in f),late_compiles=sum(int(v['compiles'])for v in f),combined_median_footprint_mib=statistics.median(sum(u['footprint_bytes']for u in v['usage'])/2**20 for v in samples),max_pressure=max(v['pressure']for v in r['samples']),swap_delta_mib=r['samples'][-1]['swap_mib']-r['samples'][0]['swap_mib'],camera=sorted({(v['cam_x'],v['cam_y'],v['cam_z'])for v in f if v['cam_x']}));assert row['late_compiles']==0;report['runs'].append(row);(e/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(row),flush=True)
  assert len({tuple(map(tuple,r['camera']))for r in report['runs']})==1
  for mode in ['baseline','fixed']:
   group=[r for r in report['runs']if r['mode']==mode];report[mode]=dict(mean_fps=statistics.mean(r['fps']for r in group),mean_gpu_ms=statistics.mean(r['gpu_ms']for r in group),mean_median_footprint_mib=statistics.mean(r['combined_median_footprint_mib']for r in group))
  report['fps_change_percent']=100*(report['fixed']['mean_fps']/report['baseline']['mean_fps']-1);report['footprint_delta_mib']=report['fixed']['mean_median_footprint_mib']-report['baseline']['mean_median_footprint_mib'];report['complete']=True
 finally:
  idle();assert sha(cpu)in [hashes['baseline'],hashes['fixed']],'external CPU change: not overwriting';install(fixed);report['restored']=sha(cpu)==hashes['fixed'];(e/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('restored fixed',report['restored'],flush=True)
 print('ABBA complete',e,flush=True)
if __name__=='__main__':main()
