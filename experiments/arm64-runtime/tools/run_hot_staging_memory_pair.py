#!/usr/bin/env python3
"""Same-build off/on late kernel snapshots, no native capture/staging audit/FPS claim."""
from pathlib import Path
import hashlib,json,re,statistics,subprocess,sys,time
P=Path(__file__).resolve().parents[1]
def main():
 e=P/'evidence'/f'hot-staging-memory-pair-{time.time_ns()}';e.mkdir();files=[P/'build/fex/Bin/bb-game-arm64',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib']
 def hashes():return {str(f):hashlib.file_digest(f.open('rb'),'sha256').hexdigest()for f in files}
 frozen=hashes();r=dict(scope='same frozen CPU/GPU, normal fixed QA, hot staging off then on, no native capture/audit; late kernel snapshots after original idle expiry; not FPS evidence',artifact_sha256=frozen,runs=[],passed=False)
 def save():(e/'result.json').write_text(json.dumps(r,indent=2)+'\n')
 print('Memory pair evidence:',e,flush=True);save()
 for hot in (False,True):
  assert hashes()==frozen
  cmd=[sys.executable,str(P/'tools/run_memory_categories.py'),'--engines','arm64','--packed-veneers','--seconds','170','--snapshot-delay-seconds','100']
  if hot:cmd+=['--hot-staging-reuse']
  print('Starting hot=',hot,flush=True);x=subprocess.run(cmd,capture_output=True,text=True,timeout=225);(e/('hot.log'if hot else'baseline.log')).write_text(x.stdout+x.stderr);assert x.returncode==0,'memory cohort failed; inspect existing output, do not relaunch blindly'
  matches=re.findall(r'^Evidence: (.+)$',x.stdout,re.M);assert len(matches)==1;cohort=Path(matches[0]);q=json.loads((cohort/'result.json').read_text());assert q['complete']and q['hot_staging_reuse']==hot and q['snapshot_delay_seconds']==100 and len(q['runs'])==1;row=q['runs'][0];assert row['exit']==0 and row['stopped']=='time budget'and not row['error']
  case=Path(row['evidence']);condition=json.loads((case/'stutter-case-conditions.json').read_text());assert not condition['staging_audit']and not condition['native_frame_capture']and not condition['audit']and not condition['sample_cpu']and condition['hot_staging_reuse']==hot
  snap=row['snapshot'];parsed=snap['parsed'];assert not parsed['warnings']and abs(parsed['deduplicated_footprint_bytes']-parsed['tool_total_footprint_bytes'])<16384
  text=(case/'runtime.log').read_text(errors='replace');assert bool(text.count('Metal staging recent reuse: enabled;'))==hot
  categories={}
  for name,allocated,count,retired,discarded,released,peak in re.findall(r'Metal memory category ([\w-]+): allocated=(\d+) bytes, objects=(\d+), retired=(\d+) bytes, discarded-capacity=(\d+) bytes, released=(\d+) bytes, peak=(\d+) bytes',text):categories[name]=dict(allocated=int(allocated),objects=int(count),retired=int(retired),discarded=int(discarded),released=int(released),peak=int(peak))
  assert 'staging'in categories
  entry=dict(hot=hot,evidence=str(cohort),case=str(case),dedup_mib=parsed['deduplicated_footprint_bytes']/2**20,per_role_footprint_mib={v['role']:v['footprint_bytes']/2**20 for v in parsed['processes']},final_capacity_categories=categories,kernel_snapshot_seconds=snap['cost_seconds']);r['runs'].append(entry);assert hashes()==frozen;save();print('hot',hot,'dedup MiB',entry['dedup_mib'],flush=True)
 a,b=r['runs'];r['hot_minus_baseline_dedup_mib']=b['dedup_mib']-a['dedup_mib'];r['passed']=True;r['limitations']='One late kernel snapshot per flag, off/on only; no universal saving/leak/FPS promise. Final log capacities are not necessarily simultaneous with kernel snapshot; original budgets/idle thresholds unchanged.';save();print('Completed memory pair:',e,flush=True)
if __name__=='__main__':main()
