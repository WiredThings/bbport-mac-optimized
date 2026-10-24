#!/usr/bin/env python3
"""Separate, non-FPS kernel snapshots for packed then original veneer layout."""
from pathlib import Path
import hashlib,json,re,statistics,subprocess,sys,time
from run_hle_lookup_abba import complete_rows
P=Path(__file__).resolve().parents[1]
def main():
 e=P/'evidence'/f'veneer-memory-pair-{time.time_ns()}';e.mkdir();files=[P/'build/fex/Bin/bb-game-arm64',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib']
 def hashes():return {str(f):hashlib.file_digest(f.open('rb'),'sha256').hexdigest()for f in files}
 frozen=hashes();report=dict(scope='paired kernel footprint and stable FEX mapping attribution, packed then legacy; not FPS evidence',artifact_sha256=frozen,runs=[],passed=False)
 def save():(e/'result.json').write_text(json.dumps(report,indent=2)+'\n')
 save();print('Pair evidence:',e,flush=True)
 try:
  for packed in (True,False):
   assert hashes()==frozen;cmd=[sys.executable,str(P/'tools/run_memory_categories.py'),'--engines','arm64','--fex-mapping-audit']
   if packed:cmd+=['--packed-veneers']
   label='packed'if packed else'legacy';print('Starting',label,flush=True)
   run=subprocess.run(cmd,capture_output=True,text=True,timeout=155);(e/(label+'.log')).write_text(run.stdout+run.stderr);assert run.returncode==0,(label,'incomplete cohort',run.returncode)
   found=re.findall(r'^Evidence: (.+)$',run.stdout,re.M);assert len(found)==1;cohort=Path(found[0]);r=json.loads((cohort/'result.json').read_text());assert r['complete']and r['packed_veneers']==packed and len(r['runs'])==1;entry=r['runs'][0];assert entry['exit']==0 and entry['stopped']=='time budget'and not entry['error']
   case=Path(entry['evidence']);policy=json.loads((case/'cpu-policy.json').read_text());assert policy['packed_veneers']==packed and policy['fast_hle_lookup'];parsed=entry['snapshot']['parsed'];assert not parsed['warnings'];assert abs(parsed['deduplicated_footprint_bytes']-parsed['tool_total_footprint_bytes'])<16384
   frames=complete_rows(case/'frames.csv');end=float(frames[-1]['t_s']);tail=[f for f in frames if float(f['t_s'])>=end-30];camera=[statistics.median(float(f[k])for f in tail if f[k])for k in ('cam_x','cam_y','cam_z')]
   if report['runs']:assert all(abs(a-b)<.001 for a,b in zip(camera,report['runs'][0]['camera']))
   row=dict(packed=packed,evidence=str(cohort),case=str(case),camera=camera,deduplicated_footprint_mib=parsed['deduplicated_footprint_bytes']/2**20,per_role_footprint_mib={p['role']:p['footprint_bytes']/2**20 for p in parsed['processes']},stable_mapping_attribution=entry['snapshot']['fex_provenance'],kernel_snapshot_seconds=entry['snapshot']['cost_seconds']);report['runs'].append(row);assert hashes()==frozen;save();print(label,round(row['deduplicated_footprint_mib'],2),'MiB',flush=True)
  packed,legacy=report['runs'];report['packed_minus_legacy_dedup_mib']=packed['deduplicated_footprint_mib']-legacy['deduplicated_footprint_mib'];report['limitations']='One snapshot per layout, not a leak trend or universal memory saving. FEX per-map resident/dirty/swapped counters are not deduplicated totals; dirty includes swapped. No FPS interpretation.';report['passed']=True;save();print('Completed:',e,flush=True)
 except BaseException as exc:report['error']=repr(exc);save();raise
if __name__=='__main__':main()
