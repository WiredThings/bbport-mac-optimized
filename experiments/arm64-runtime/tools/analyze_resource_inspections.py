#!/usr/bin/env python3
"""Correlate renderer metadata with bounded, read-only CPU/physical-alias queries."""
from pathlib import Path
import argparse,collections,json

def main():
 ap=argparse.ArgumentParser();ap.add_argument('evidence',type=Path);a=ap.parse_args();p=a.evidence;travel=json.loads((p/'travel-result.json').read_text());counts=collections.Counter();phases=[];unique={};callees=collections.Counter();examples=[]
 for file in travel.get('cpu_inspections',[]):
  d=json.loads(Path(file).read_text());selected=json.loads((p/f'resource-selection-{d["sequence"]}.json').read_text());assert len(selected)==len(d['queries']);stats=collections.Counter()
  for texture,q in zip(selected,d['queries']):
   assert int(texture['address'])==q['address']and int(texture['guest_bytes'])==q['size']
   kind='known_control'if int(texture['current_owner'])else'unknown_sampled';unique[(texture['uid'],q['address'],q['size'])]=kind
   stats[kind+'_queries']+=1;stats[kind+'_cpu_covering_owner']+=bool(q['cpu_owner']['generation']);stats[kind+'_cpu_any_overlap']+=bool(q['cpu_heap_overlaps']or q['cpu_range_overlaps']);stats[kind+'_extra_alias']+=len(q['aliases'])>1;stats[kind+'_any_alias_owner']+=any(x['cpu_owner']['generation']for x in q['aliases']);stats[kind+'_consistent_mapping']+=q['mapping_consistent'];stats[kind+'_truncated']+=q['aliases_truncated']or q['overlaps_truncated']or any(x['overlaps_truncated']for x in q['aliases']);stats[kind+'_gpu_visible_direct']+=q['mapped']and q['kind']==2 and q['prot']&0x30!=0
   source=q.get('source_allocation',{});stats[kind+'_loader_allocation_match']+=bool(source.get('sequence'))
   if source.get('sequence'):
    callees[hex(source['callee_offset'])]+=1
    stats[kind+'_loader_exact_base']+=source['address']==q['address']
    stats[kind+'_loader_exact_size']+=source['size']==q['size']
    stats[kind+'_texture_return_match']+=source.get('kind')==2
    if len(examples)<8:examples.append(dict(snapshot=d['sequence'],uid=texture['uid'],host_capacity_bytes=int(texture['host_bytes']),query=q))
  counts.update(stats);phases.append(dict(snapshot=d['sequence'],file=file,counts=dict(stats)))
 report=dict(scope='Shared-only diagnostic metadata, no reclaim authorization or FPS comparison',travel_passed=travel['passed'],counts=dict(counts),unique_image_range_pairs=len(unique),unique_unknown_image_range_pairs=sum(v=='unknown_sampled'for v in unique.values()),phases=phases,observed_loader_callees=dict(callees),matched_examples=examples,limits=['selected largest unknown sampled textures plus known controls; not whole-game coverage','CPU and renderer metadata sampled at nearby times; not atomic joint ownership proof','only current shared direct aliases are enumerated; historical aliases/private/cross-VMA spans are not absence proofs','historical loader span containment does not prove current ownership or identity after free/reuse','missing allocations and cold/unknown textures remain protected; no cache shrinking or new reclaim rule'])
 (p/'resource-inspection-summary.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items()if k not in ['phases','matched_examples','limits']},indent=2))
if __name__=='__main__':main()
