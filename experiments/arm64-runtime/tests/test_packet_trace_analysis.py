#!/usr/bin/env python3
"""Authored trace format/symbol/hash rejection test; never gameplay evidence."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys,time,re
P=Path(__file__).resolve().parents[1];e=P/'evidence'/('authored-packet-trace-'+str(time.time_ns()));e.mkdir()
library=P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib';digest=hashlib.file_digest(library.open('rb'),'sha256').hexdigest()
symbols=subprocess.check_output(['/usr/bin/nm','-an',str(library)],text=True);match=re.search(r'^([0-9a-fA-F]+) [tT] (.+RunDrawPacket.+)$',symbols,re.M);assert match;target=int(match[1],16)
assert re.search(r'segname __TEXT\s+vmaddr 0x0+',subprocess.check_output(['/usr/bin/otool','-l',str(library)],text=True))
artifact=dict(gpu_library=str(library),sha256=digest);(e/'packet-audit-artifacts.json').write_text(json.dumps(artifact));(e/'result.json').write_text(json.dumps(dict(exit=0,stopped='time budget',authored_trace_fixture=True)))
rows=[(19,1000000000,0,0,10),(17,2000000000,100000000,0,16),(18,2000000000,100000000,1000000,500000),(43,2000000000,100000000,0,(2<<32)|16),(44,2000000000,100000000,0,target),(40,2000000000,100000000,0,1),(19,3000000000,0,0,100|(1<<32))]
data=bytearray(64+16384*64);struct.pack_into('<8s7Q',data,0,b'BBAUDIT1',1,123,1,1,16384,len(rows),0)
for seq,(kind,start,wall,cpu,arg)in enumerate(rows,1):struct.pack_into('<QII6Q',data,64+(seq-1)*64,seq,0,kind,456,start,wall,cpu,arg,0)
trace=e/'stutter-123.bin';trace.write_bytes(data)
def run():return subprocess.run([sys.executable,str(P/'tools/resolve_packet_tasks.py'),str(e)],capture_output=True,text=True)
r=run();assert r.returncode==0,r.stderr;report=json.loads((e/'packet-identity-summary.json').read_text());assert report['authored_trace_fixture'];task=report['slow_packets'][0];assert task['kind']=='ordered-task'and task['symbol']['exact_entry']and task['cpu_bounds_ms']==dict(lower=.5,upper=1.0,slack=.5);assert report['long_frame_coverage'][0]['groups'][0]['no_rejected_samples_in_bracket']
# An in-flight/unpublished status row must remove any coverage proof.
struct.pack_into('<I',data,64+8,1);trace.write_bytes(data);r=run();assert r.returncode==0,r.stderr;report=json.loads((e/'packet-identity-summary.json').read_text());assert not report['traces'][0]['all_published'];assert not any(g['no_rejected_samples_in_bracket']for f in report['long_frame_coverage']for g in f['groups'])
# Wrong hash must fail before naming a task; never mutate the real library.
artifact['sha256']='0'*64;(e/'packet-audit-artifacts.json').write_text(json.dumps(artifact));r=run();assert r.returncode!=0 and 'library changed'in r.stderr
assert hashlib.file_digest(library.open('rb'),'sha256').hexdigest()==digest
out=dict(passed=True,authored_trace_fixture=True,scope='parser and hash contract only, not a game/FPS/network test',checks=['task kind/function exact symbol','CPU bounds','complete publication bracket','unpublished row denies proof','different binary hash refused']);(e/'test-result.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(dict(evidence=str(e),**out)))
