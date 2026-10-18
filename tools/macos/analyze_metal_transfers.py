#!/usr/bin/env python3
"""Read bounded native copy metadata and optional compile diagnostics. Never infer dead copies."""
import argparse
from collections import Counter, defaultdict
import importlib.util
import json
from pathlib import Path
import re

spec=importlib.util.spec_from_file_location('attachment_audit',Path(__file__).with_name('analyze_metal_attachments.py'))
attachments=importlib.util.module_from_spec(spec);spec.loader.exec_module(attachments)


def analyze(text):
    summary=attachments.analyze(text)  # Validates complete frame and native copy bounds.
    names={};groups=defaultdict(lambda:dict(regions=0,bytes=0,flush_regions=0))
    regions=[];seen=Counter()
    for line in text.splitlines():
        if not line.startswith('NATIVE '):continue
        row=json.loads(line[7:]);queue=row.get('queue')
        if row['op']=='label':
            for side in ('source','destination'):
                prefix='Buffer copy '+side+': '
                if row['text'].startswith(prefix):names[queue,side]=row['text'][len(prefix):]
        elif row['op']=='buffer_copy':
            source=names.get((queue,'source'),'unknown');destination=names.get((queue,'destination'),'unknown')
            # Group labels, not guest addresses. Exact resource/range identity remains in regions.
            normalized=lambda s:re.sub(r'0x[0-9a-fA-F]+','<address>',s)
            key=(normalized(source),normalized(destination));group=groups[key]
            group['regions']+=1;group['bytes']+=row['bytes'];group['flush_regions']+=row['flush']
            identity=tuple(row[k] for k in ('queue','src','dst','src_offset','dst_offset','bytes'))
            seen[identity]+=1
            regions.append(dict(row,source_label=source,destination_label=destination))
    return dict(frame=summary['frame'],encoders=summary['encoders'],copy_totals=summary['buffer_copies'],
        groups=[dict(source=k[0],destination=k[1],**v) for k,v in sorted(groups.items(),key=lambda x:-x[1]['bytes'])],
        repeated_exact_range_occurrences=sum(v-1 for v in seen.values()),regions=regions,
        notes=['Region count is not API-call count or GPU duration.',
               'Scope is Runtime::CopyBuffer; this is not a total of all image, upload, direct-native or implicit driver transfers.',
               'Repeated ranges may have intervening writes. No copy is proven redundant.',
               'Native object identity does not establish underlying allocation nonaliasing.'])


def compiles(text):
    if 'Metal compile audit: truncated=1' in text:raise ValueError('Compile diagnostics truncated')
    pattern=r'Metal compile audit: kind=(\w+) id=([0-9a-f]+) start-ns=(\d+) elapsed-us=(\d+) ok=([01]) cached=(-?[01])'
    events=[dict(kind=m[1],id=m[2],start_ns=int(m[3]),elapsed_us=int(m[4]),ok=m[5]=='1',cached=int(m[6])) for m in re.finditer(pattern,text)]
    groups=[]
    for kind in sorted({e['kind'] for e in events}):
        entries=[e for e in events if e['kind']==kind]
        groups.append(dict(kind=kind,count=len(entries),total_us=sum(e['elapsed_us'] for e in entries),
            max_us=max(e['elapsed_us'] for e in entries),failed=sum(not e['ok'] for e in entries),
            cache_hits=sum(e['cached']==1 for e in entries) if kind!='library' else None))
    return dict(groups=groups,slowest=sorted(events,key=lambda e:-e['elapsed_us'])[:20],
        notes=['Shader spans contain library spans: do not add nested group totals.',
               'Shader cached means MSL text cache; PSO cached means binary-archive hit.',
               'Library API time does not prove a driver cache miss or GPU execution cost.'])


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--log',type=Path);args=p.parse_args()
    result=analyze(args.capture.read_text())
    if args.log:result['compile_audit']=compiles(args.log.read_text(errors='replace'))
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
