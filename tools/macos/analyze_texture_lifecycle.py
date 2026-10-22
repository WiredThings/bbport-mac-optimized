#!/usr/bin/env python3
"""Compare private, same-process texture inventories; never decides what can be freed."""
import argparse
import csv
import json
from pathlib import Path

CONTENT=('uploads','clears','copies','draws','storage_binds','unknown_writes')
REQUIRED=('uid','host_bytes','flags','tag_state','lru_tick','gc_tick','gpu_complete')
STATES={0:'unknown',1:'live',2:'released',3:'reused'}

def read_snapshot(path):
    path=Path(path)
    if path.stat().st_size>8*1024*1024:raise ValueError('Inventory exceeds 8 MiB bound')
    rows={}
    with path.open(newline='') as stream:
        reader=csv.DictReader(stream)
        if not all(k in (reader.fieldnames or []) for k in REQUIRED):raise ValueError('Missing inventory columns')
        content=all(k in reader.fieldnames for k in CONTENT)
        for row in reader:
            if len(rows)>=16384:raise ValueError('Inventory exceeds 16384 row bound')
            item={k:int(row[k]) for k in REQUIRED}
            if any(v<0 for v in item.values()) or item['uid'] and item['uid'] in rows:raise ValueError('Negative or duplicate inventory identity')
            # Registered descriptors can have no native allocation yet. UID zero
            # is not an identity; preserve its zero-byte count, never its survival.
            if not item['uid'] and item['host_bytes']:raise ValueError('Unidentified native backing')
            if content:
                item.update({k:int(row[k]) for k in CONTENT})
                if any(item[k]<0 for k in CONTENT):raise ValueError('Negative content counter')
            rows[item['uid'] if item['uid'] else -(len(rows)+1)]=item
    return rows

def summarize(rows):
    result=dict(objects=len(rows),unidentified_zero_backing_objects=sum(not x['uid'] for x in rows.values()),host_bytes=sum(x['host_bytes'] for x in rows.values()),states={})
    for code,name in STATES.items():
        group=[x for x in rows.values() if x['tag_state']==code]
        result['states'][name]=dict(objects=len(group),host_bytes=sum(x['host_bytes'] for x in group))
    result['gpu_modified_bytes']=sum(x['host_bytes'] for x in rows.values() if x['flags']&8)
    result['completed_cold600_gpu_modified_bytes']=sum(x['host_bytes'] for x in rows.values()
        if x['flags']&8 and x['gpu_complete'] and x['gc_tick']-x['lru_tick']>=600)
    observed=[x for x in rows.values() if all(k in x for k in CONTENT)]
    result['content']=dict(observed_objects=len(observed),missing_objects=len(rows)-len(observed),
        encoded_nonupload_write_bytes=sum(x['host_bytes'] for x in observed if any(x[k] for k in ('clears','copies','draws','unknown_writes'))),
        potential_storage_write_bytes=sum(x['host_bytes'] for x in observed if x['storage_binds']),
        counters={k:sum(x[k] for x in observed) for k in CONTENT})
    return result

def analyze(paths):
    if not 2<=len(paths)<=32:raise ValueError('Use 2..32 snapshots from one private process')
    # One audit directory belongs to exactly one controlled process. UIDs are not
    # comparable across restarts, even if addresses and camera positions agree.
    if len({Path(p).resolve().parent for p in paths})!=1:raise ValueError('Inventories must share one private audit directory')
    first=previous=None;points=[]
    for path in paths:
        rows=read_snapshot(path);summary=summarize(rows);summary['path']=str(path)
        if first is None:first=rows
        if previous is not None:
            current_ids={u for u in rows if u>0};previous_ids={u for u in previous if u>0}
            summary['since_previous']=dict(added_uids=len(current_ids-previous_ids),removed_uids=len(previous_ids-current_ids),
                retained_uids=len(current_ids&previous_ids),host_byte_delta=summary['host_bytes']-sum(x['host_bytes'] for x in previous.values()))
        points.append(summary);previous=rows
    kept={u for u in first.keys()&previous.keys() if u>0}
    return dict(points=points,initial_uids_still_present=len(kept),initial_surviving_host_bytes=sum(previous[u]['host_bytes'] for u in kept),
        notes=['Image UID survival is distinct from native-backing survival; backing replacement can reset counters.',
               'Live/unknown/reused tags, coldness, and GPU-complete status alone do not permit reclamation.',
               'Counters are positive command provenance, not per-pixel coverage, lifetime deltas or a complete write trace.',
               'Metadata diagnostics are not scored FPS or process-footprint measurements.'])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('snapshots',type=Path,nargs='+')
    print(json.dumps(analyze(p.parse_args().snapshots),indent=2))
