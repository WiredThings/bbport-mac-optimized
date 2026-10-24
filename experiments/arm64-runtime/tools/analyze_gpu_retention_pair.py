#!/usr/bin/env python3
"""Accept only count-closed native inventories; never infer freeable from idle."""
from pathlib import Path
import argparse,collections,json,re

def capture(path):
 rows=[json.loads(l[7:])for l in path.read_text().splitlines()if l.startswith('NATIVE ')]
 def one(op):
  found=[x for x in rows if x.get('op')==op];assert len(found)==1,(op,len(found));return found[0]
 frame=one('frame');header=one('memory_lifetimes');memory=[x for x in rows if x.get('op')=='memory_live'];staging=[x for x in rows if x.get('op')=='staging_lifetime'];kind=[x for x in memory if x['kind']==1]
 assert not header['live_overflow']and len(memory)==header['live_count']and len(memory)<=2048
 assert len({x['native']for x in memory})==len(memory)and len({x['uid']for x in memory})==len(memory)
 assert len({x['native']for x in staging})==len(staging)and {x['native']for x in staging}=={x['native']for x in kind}
 assert sum(x['bytes']for x in staging)==sum(x['bytes']for x in kind)
 assert not any(x.get('op')=='lifetime_overflow'and x.get('kind')=='staging'for x in rows)
 target=[x for x in memory if x['kind']==3];old=[x for x in target if x['content_us']and header['now_us']-x['content_us']>30e6]
 return dict(file=str(path),inventory_accepted=True,full_frame_accepted=not frame['truncated'],full_frame_truncated=frame['truncated'],inventory_live_count=len(memory),destroyed_history_total=header['destroyed_total'],destroyed_history_ring=header['destroyed_ring'],destroyed_history_overwritten=header['destroyed_total']>header['destroyed_ring'],staging_count=len(staging),staging_capacity_mib=sum(x['bytes']for x in staging)/2**20,staging_discarded_capacity_mib=sum(x['bytes']for x in staging if x['discarded'])/2**20,staging_ring_max_age=max((x['age_ticks']for x in staging if not x['large']),default=0),staging_large_capacity_mib=sum(x['bytes']for x in staging if x['large'])/2**20,render_targets_count=len(target),render_target_capacity_mib=sum(x['bytes']for x in target)/2**20,render_target_encoded_content_idle30s_capacity_mib=sum(x['bytes']for x in old)/2**20,staging=staging)

def main():
 ap=argparse.ArgumentParser();ap.add_argument('cohort',type=Path);a=ap.parse_args();p=a.cohort;r=json.loads((p/'result.json').read_text());assert r['complete']and r['runs'];out=dict(scope='complete metadata subinventories only; truncated full frames are explicitly rejected; no FPS conclusion',complete=False,hot_staging_reuse=r.get('hot_staging_reuse',False),artifact_sha256=r['artifact_sha256'],runs=[])
 for run in r['runs']:
  assert run['runtime_complete']and run['correct_policy']and not run['error']and len(run['captures'])==2
  frames=[capture(Path(x))for x in run['captures']];case=Path(run['case']);text=(case/'runtime.log').read_text(errors='replace');rs=[tuple(map(int,m))for m in re.findall(r'Metal staging requests: frame=(\d+) type=(\d+) calls=(\d+) bytes=(\d+) medium-calls=(\d+) medium-bytes=(\d+)',text)];rates=[]
  for t in range(3):
   xs=[x for x in rs if x[1]==t and x[0]>=1800];assert len(xs)>1;x,y=xs[0],xs[-1];df=y[0]-x[0];rates.append(dict(type=t,start_frame=x[0],end_frame=y[0],calls_per_frame=(y[2]-x[2])/df,mib_per_frame=(y[3]-x[3])/df/2**20,medium_per_frame=(y[4]-x[4])/df))
  out['runs'].append(dict(engine=run['engine'],case=str(case),frames=frames,steady_staging_rates=rates))
 out['complete']=True;out['limitations']=['Encoded content idle is not absence of reads/references or proof of freeability.','Capture boundary drains can perturb the workload; diagnostic capacities are not normal FPS/memory benefits.','Destroyed history is bounded; overwritten records cannot prove no destruction.','Only complete independently count-closed memory/staging inventories accepted; no whole-frame claims for truncated traces.','No texture pixel or payload export; quality and cache budgets unchanged.']
 (p/'analysis.json').write_text(json.dumps(out,indent=2)+'\n');print('Inventory accepted:',p,flush=True)
 for row in out['runs']:
  print(row['engine'],[(round(f['staging_capacity_mib'],2),round(f['staging_discarded_capacity_mib'],2),f['staging_count'])for f in row['frames']],flush=True)
if __name__=='__main__':main()
