#!/usr/bin/env python3
"""Summarize bounded native attachment metadata. Does not infer complete resource liveness."""
import argparse
from collections import Counter
import json
from pathlib import Path


def analyze(text):
    rows=[json.loads(line[7:]) for line in text.splitlines() if line.startswith('NATIVE ')]
    if not rows or rows[0].get('op')!='frame' or rows[0].get('truncated',True):
        raise ValueError('Missing or truncated single-frame metadata')
    nodes=[];current={};render_barriers=[];barrier_paths=Counter();query_snapshots=[];compute_access=[];texture_tiling=[]
    for row in rows[1:]:
        op=row['op'];queue=row.get('queue')
        if op=='open':
            if queue in current:raise ValueError('Overlapping encoders on one queue')
            node=dict(row,attachments=[],labels=[]);nodes.append(node);current[queue]=node
        elif op=='close':
            if queue not in current or current[queue]['kind']!=row['kind']:
                raise ValueError('Incomplete encoder boundary')
            del current[queue]
        elif op=='barrier':
            barrier_paths[(row.get('kind'),row.get('holding'),row.get('hoisting'))]+=1
        elif op=='occlusion_snapshot':
            if queue not in current or current[queue]['kind']!=2:
                raise ValueError('Occlusion snapshot without a compute encoder')
            if not (isinstance(row.get('count'),int) and 0<=row['count']<=4096 and isinstance(row.get('pairs'),int) and 1<=row['pairs']<=16 and row.get('end') in (0,1) and row.get('poison') in (0,1) and isinstance(row.get('chained'),bool)):
                raise ValueError('Invalid occlusion snapshot counters')
            query_snapshots.append(row)
        elif op in ('compute_access','texture_tiling'):
            if queue not in current or current[queue]['kind']!=2:
                raise ValueError('Compute resource event without a compute encoder')
            if op=='compute_access':
                flags=('direct_resources','requested_bound_only','guest_declared','bda','paged','indirect')
                if any(not isinstance(row.get(k),bool) for k in flags):raise ValueError('Invalid compute resource flags')
                if row['direct_resources'] and row['bda']:raise ValueError('Indirect memory classified as direct')
                if row['requested_bound_only'] and not row['direct_resources']:raise ValueError('Unproven compute resource restriction')
                if not row['guest_declared'] and not row['requested_bound_only']:raise ValueError('Missing required compute guest residency')
                compute_access.append(row)
            else:
                if row.get('bits') not in (32,64) or not isinstance(row.get('proxy'),bool):raise ValueError('Invalid texture tiling kind')
                if any(not isinstance(row.get(k),int) or row[k]<=0 for k in ('width','height','linear_bytes')):raise ValueError('Invalid texture tiling extent')
                texture_tiling.append(row)
        elif op=='render_barriers':
            if queue not in current or current[queue]['kind']!=1:
                raise ValueError('Render barrier summary without a render encoder')
            requested,dropped=row['requested'],row['dropped']
            if not isinstance(requested,int) or not isinstance(dropped,int) or not 0<=dropped<=requested:
                raise ValueError('Invalid render barrier counters')
            render_barriers.append(row)
        elif op=='attachment':
            if queue not in current:raise ValueError('Attachment without an encoder')
            current[queue]['attachments'].append(row)
        elif op=='label' and queue in current:
            current[queue]['labels'].append(row['text'])
    if current:raise ValueError('Unclosed encoder at frame boundary')
    counts=Counter(n['kind'] for n in nodes)
    clears=[]
    for i,node in enumerate(nodes):
        a=node['attachments']
        if node['kind']==1 and not node['guest'] and a and all(x['slot']>=0 and x['load']==2 and x['store']==1 for x in a):
            clears.append(dict(encoder=i,targets=len(a),size=[a[0]['w'],a[0]['h']],queue=node['queue']))
    runs=[];active=[]
    for c in clears:
        if active and (c['encoder']!=active[-1]['encoder']+1 or c['size']!=active[-1]['size'] or c['queue']!=active[-1]['queue']):
            if len(active)>1:runs.append(active)
            active=[]
        active.append(c)
    if len(active)>1:runs.append(active)
    return dict(frame=rows[0]['frame'],metadata_bytes=rows[0].get('bytes'),encoders=len(nodes),
                render=counts[1],compute=counts[2],blit=counts[3],attachments=sum(len(n['attachments']) for n in nodes),
                full_color_clear_passes=clears,consecutive_clear_runs=runs,
                render_barriers=dict(lists=len(render_barriers),requested=sum(r['requested'] for r in render_barriers) if render_barriers else None,dropped=sum(r['dropped'] for r in render_barriers) if render_barriers else None),
                occlusion_snapshots=dict(events=len(query_snapshots),chained_events=sum(q['chained'] for q in query_snapshots),counter_slots=sum(q['count'] for q in query_snapshots)) if query_snapshots else None,
                compute_resource_access=dict(dispatches=len(compute_access),direct_resources=sum(r['direct_resources'] for r in compute_access),bound_only_requested=sum(r['requested_bound_only'] for r in compute_access),broad_residency_at_dispatch=sum(r['guest_declared'] for r in compute_access)) if compute_access else None,
                texture_tiling=dict(copies=len(texture_tiling),half_copies=sum(r['bits']==64 for r in texture_tiling),proxy_copies=sum(r['proxy'] for r in texture_tiling),linear_bytes_avoided=sum(r['linear_bytes'] for r in texture_tiling)) if texture_tiling else None,
                scheduler_barrier_requests_by_path=[dict(kind=k[0],holding=k[1],hoisting=k[2],count=count) for k,count in barrier_paths.items()],
                notes=['Decoded guest GPU frame; not a replayable GPU trace.','No texture/buffer payload snapshots.','Adjacent barrier metadata can hide intervening GPU draws or dispatches; it is not evidence that a dependency is redundant.',
                       'Clear-load candidates may contain draws; corroborate labels and source before treating a pass as clear-only.',
                       'Shader/compute/blit/CPU and alias readers are not fully represented; this summary is not proof that an attachment store can be discarded.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('capture',type=Path);args=parser.parse_args()
    print(json.dumps(analyze(args.capture.read_text()),indent=2))


if __name__=='__main__':main()
