#!/usr/bin/env python3
from pathlib import Path
import sys,json,time
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'tools'));from fex_map_provenance import lifecycle,regions,associate
kernel='''Process: owned [123]
VM page size:  16384 bytes
Untagged 100000000-100010000 [4 1 1 2] rw-/rwx SM=PRV
Untagged 100010000-100020000 [4 0 0 0] ---/rwx SM=NUL
'''
def event(n,action='A',addr='0x100000000',size=65536,kind=2):return f'FEX_MAP pid=123 seq={n} mono={n*1000} action={action} kind={kind} address={addr} bytes={size}\n'
before=event(1);after=before;r=associate(before,after,kernel,123);assert r['categories']['thread-lookup']['resident_bytes']==16384 and r['categories']['thread-lookup']['swapped_bytes']==32768 and r['unattributed']['regions']==1
checkpoint='FEX_MAP_CHECKPOINT pid=123 mono=9000 issued=1 failed=0\n'
r=associate(before,after+checkpoint,kernel,123,required_after_mono_ns=8000);assert r['publication_checkpoint']['issued']==1
for tail in ['',checkpoint.replace('issued=1','issued=2'),checkpoint.replace('failed=0','failed=1'),checkpoint.replace('mono=9000','mono=7000')]:
 try:associate(before,after+tail,kernel,123,required_after_mono_ns=8000)
 except ValueError:pass
 else:raise AssertionError('missing/failed/stale/tail checkpoint accepted')
# Same address reuse between snapshots is not a stable owner.
r=associate(before,before+event(2,'F')+event(3),kernel,123);assert r['stable_allocations']==0 and r['unattributed']['regions']==2
r=associate(before,after,kernel.replace('[4 1 1 2]','[4 0.03 0.03 2]'),123);assert r['categories']['thread-lookup']['resident_bytes']==0 and r['unattributed']['unquantified_regions']==1
# Entire-region containment only; never apportion residency across a partial label.
r=associate(event(1,size=32768),event(1,size=32768),kernel,123);assert r['partial_regions_unattributed']==1 and r['categories']['thread-lookup']['resident_bytes']==0
for text in [event(2),before+event(1),before+event(2,'A'),event(1,'F'),before+event(2,'F',size=32768),before+event(2,addr='0x100008000'),before+'FEX_MAP_INVALID pid=123 seq=2\n',before+'FEX_MAP_OVERFLOW pid=123 seq=16385\n']:
 try:lifecycle(text,123)
 except ValueError:pass
 else:raise AssertionError(text)
for text,pid in [(kernel,124),(kernel.replace('16384 bytes','8192 bytes'),123),(kernel.replace('[4 1 1 2]','[4 9 1 2]'),123),(kernel+kernel.splitlines()[-2]+'\n',123)]:
 try:regions(text,pid)
 except ValueError:pass
 else:raise AssertionError('bad kernel input accepted')
# Validate actual tool output from our own terminated sleep fixture, not game data.
fixture=P/'evidence/authored-vmmap-regions-1792798298877535000/regions.txt';real,page=regions(fixture.read_text(),74467);assert real and page==16384
out=P/'evidence'/f'authored-fex-map-analysis-{time.time_ns()}';out.mkdir();r=dict(passed=True,scope='synthetic owner lifecycle and actual owned-sleep kernel-format parsing, not gameplay',checks=['same-address generation reuse','unknown/mismatched frees and overlapping owners','partial-region residency stays unknown','missing/duplicate/invalid/overflow events rejected','PID/page/size/overlap rejection','actual own-process vmmap page rows']);(out/'test-result.json').write_text(json.dumps(r,indent=2)+'\n');print(dict(evidence=str(out),**r))
