#!/usr/bin/env python3
"""Summarize load/retention evidence without inferring release from missing owners."""
from pathlib import Path
import argparse,csv,json,re,statistics

def main():
 ap=argparse.ArgumentParser();ap.add_argument('evidence',type=Path);a=ap.parse_args();e=a.evidence;travel=json.loads((e/'travel-result.json').read_text());runtime=json.loads((e/'result.json').read_text());events=[json.loads(l)for l in(e/'travel-events.jsonl').read_text().splitlines()]
 out=dict(evidence=str(e),passed=travel['passed'],scope='load correctness/metadata; no performance comparison',seconds=runtime['seconds'],completed_moves=len(travel['completed']),pressure_max=max(s['pressure']for s in runtime['samples']),swap_net_mib=runtime['samples'][-1]['swap_mib']-runtime['samples'][0]['swap_mib'],snapshots=[])
 for sequence,p in enumerate(sorted((e/'texture-lifetimes').glob('textures-*.csv'),key=lambda p:int(p.stem.split('-')[-1])),1):
  rows=list(csv.DictReader(p.open()));assert rows and all(len(r)==40 and None not in r and all(v is not None for v in r.values())for r in rows)
  selected=[r for r in rows if int(r['usage'])&1 and not(int(r['usage'])&12)]
  def group(rows):return dict(images=len(rows),host_capacity_mib=sum(int(r['host_bytes'])for r in rows)/2**20)
  entry=dict(snapshot=sequence,path=str(p),all_images=len(rows),all_host_capacity_mib=sum(int(r['host_bytes'])for r in rows)/2**20,sampled_non_target=group(selected),sampled_no_tracked_overlap=group([r for r in selected if not(int(r['current_owner'])or int(r['heap_live_overlaps'])or int(r['range_live_overlaps']))]))
  out['snapshots'].append(entry)
 log=(e/'runtime.log').read_text();out['reported_metadata_failure']='ARM RESOURCE invalid metadata'in log;out['reported_fatal']='ARM FATAL'in log or 'ARM HLE STOP'in log
 for domain in ['range','heap']:
  pattern=rf'Guest resource lifetime {domain}: allocations=(\d+), releases=(\d+), live-ranges=(\d+), live-bytes=(\d+), address-reuses=(\d+), overlapping-replacements=(\d+), unknown-frees=(\d+), dropped=(\d+)'
  rows=[dict(zip(('allocations','releases','live_ranges','live_bytes','reuses','overlaps','unknown_frees','dropped'),map(int,m)))for m in re.findall(pattern,log)]
  out[domain+'_reported_first_last']=[rows[0],rows[-1]]if rows else[]
 out['warning']='Host capacity is allocation accounting, not resident RAM. Different scenes may retain valid caches. Unknown owner does not prove freed. Repeated growth is a diagnostic target, not permission to reclaim. These map changes do not replace enemy-death/authenticated network tests.'
 (e/'travel-summary.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({k:v for k,v in out.items()if k not in ['snapshots','warning']},indent=2));print('Sampled MiB:',[round(r['sampled_non_target']['host_capacity_mib'],2)for r in out['snapshots']])
if __name__=='__main__':main()
