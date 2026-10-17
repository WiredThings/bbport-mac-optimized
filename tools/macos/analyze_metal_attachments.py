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
    nodes=[];current={}
    for row in rows[1:]:
        op=row['op'];queue=row.get('queue')
        if op=='open':
            if queue in current:raise ValueError('Overlapping encoders on one queue')
            node=dict(row,attachments=[],labels=[]);nodes.append(node);current[queue]=node
        elif op=='close':
            if queue not in current or current[queue]['kind']!=row['kind']:
                raise ValueError('Incomplete encoder boundary')
            del current[queue]
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
                notes=['Decoded guest GPU frame; not a replayable GPU trace.','No texture/buffer payload snapshots.',
                       'Clear-load candidates may contain draws; corroborate labels and source before treating a pass as clear-only.',
                       'Shader/compute/blit/CPU and alias readers are not fully represented; this summary is not proof that an attachment store can be discarded.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('capture',type=Path);args=parser.parse_args()
    print(json.dumps(analyze(args.capture.read_text()),indent=2))


if __name__=='__main__':main()
