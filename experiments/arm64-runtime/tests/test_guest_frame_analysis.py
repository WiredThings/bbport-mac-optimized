#!/usr/bin/env python3
"""Authored publication-interval/guest-accounting tests, never gameplay evidence."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys,time
P=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(P/'tools'))
from analyze_stutter import read,publication_range
from analyze_guest_frames import frame_cpu_bounds
E=P/'evidence'/('authored-guest-frame-'+str(time.time_ns()));E.mkdir()
artifacts=[P/'build/fex/Bin/bb-game-arm64',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib']
hashes={str(p):hashlib.file_digest(p.open('rb'),'sha256').hexdigest()for p in artifacts}
(E/'guest-frame-artifacts.json').write_text(json.dumps(hashes));(E/'runtime.log').write_text('BACHATA_FEX_HLE_BEGIN operation=382 name=gpu.sceGnmSubmitCommandBuffers\nBACHATA_FEX_HLE_BEGIN operation=383 name=gpu.sceGnmSubmitAndFlipCommandBuffers\n')
(E/'result.json').write_text(json.dumps(dict(exit=0,stopped='time budget',authored_trace_fixture=True)))
cap=16384;issued=cap+10
cpu=bytearray(64+cap*64);struct.pack_into('<8s7Q',cpu,0,b'BBAUDIT1',1,123,1,1,cap,issued,0)
def put(data,seq,kind,tid,start,wall=0,used=0,arg=0):struct.pack_into('<QII6Q',data,64+((seq-1)%cap)*64,seq,0,kind,tid,start,wall,used,arg,0)
for n in range(11,issued+1):put(cpu,n,99,999,n*1000000)
put(cpu,98,45,456,800000000,arg=383)
put(cpu,100,47,456,1000000000,arg=10|(1<<48));put(cpu,101,48,456,1000000000,arg=10|(10<<32))
put(cpu,102,46,456,1950000000,50000000,5000000,382);put(cpu,103,45,456,2000000000,arg=383)
put(cpu,104,47,456,3000000000,arg=12|(2<<48));put(cpu,105,48,456,3000000000,arg=12|(12<<32));put(cpu,106,45,456,3100000000,arg=383)
gpu=bytearray(64+cap*64);struct.pack_into('<8s7Q',gpu,0,b'BBAUDIT1',1,124,1,1,cap,1,0);put(gpu,1,40,457,1900000000,200000000,arg=1)
(E/'stutter-124.bin').write_bytes(gpu)
def run(data=cpu,valid=True):
 (E/'stutter-123.bin').write_bytes(data)
 r=subprocess.run([sys.executable,str(P/'tools/analyze_guest_frames.py'),str(E)],capture_output=True,text=True);assert r.returncode==0,r.stderr
 s=json.loads((E/'guest-frame-summary.json').read_text());assert s['authored_trace_fixture'];f=s['long_frames'][0];c=f['guest_threads'][0]['coverage'];assert c['valid']==valid,(c,valid)
 return s,c
s,c=run();assert not s['trace_quality'][0]['all_published']and c['publication_range']['valid']and not c['publication_range']['whole_trace_complete'];assert s['long_frames'][0]['selected_submit_wall_union_ms']==50
for missing in [100,101,102,103,104,105]:
 bad=bytearray(cpu);struct.pack_into('<I',bad,64+((missing-1)%cap)*64+8,1)
 # A missing status removes the bracket rather than fabricating one.
 if missing in [100,104]:
  (E/'stutter-123.bin').write_bytes(bad);r=subprocess.run([sys.executable,str(P/'tools/analyze_guest_frames.py'),str(E)],capture_output=True,text=True);assert r.returncode==0,r.stderr;s=json.loads((E/'guest-frame-summary.json').read_text());assert not s['long_frames'][0]['guest_threads'][0].get('coverage',{}).get('valid')
 else:run(bad,False)
bad=bytearray(cpu);struct.pack_into('<Q',bad,56,1);struct.pack_into('<Q',bad,64+((12-1)%cap)*64,0);s,c=run(bad);assert s['trace_quality'][0]['dropped']==1
# Retained-range holes fail even when counters themselves still look valid.
bad=bytearray(cpu);struct.pack_into('<Q',bad,64+((102-1)%cap)*64,0);run(bad,False)
for value in [12|(11<<32),13|(13<<32)]:
 bad=bytearray(cpu);put(bad,105,48,456,3000000000,arg=value);run(bad,False)
for arg in [12|(1<<32)|(2<<48),9|(2<<48),12|(65535<<48),12|(3<<48)]:
 bad=bytearray(cpu);put(bad,104,47,456,3000000000,arg=arg);run(bad,False)
(E/'stutter-123.bin').write_bytes(cpu);t=read(E/'stutter-123.bin');assert not publication_range(t,1,10)['valid'];assert not publication_range(t,0,10)['valid'];assert not publication_range(t,104,100)['valid'];assert not publication_range(t,issued,issued+1)['valid'];assert publication_range(t,100,105)['valid']
wrong=dict(hashes);wrong[str(artifacts[0])]='0'*64;(E/'guest-frame-artifacts.json').write_text(json.dumps(wrong));r=subprocess.run([sys.executable,str(P/'tools/analyze_guest_frames.py'),str(E)],capture_output=True,text=True);assert r.returncode!=0 and 'artifact changed'in r.stderr
(E/'guest-frame-artifacts.json').write_text(json.dumps(hashes));run()
def win(start,wall,cpu):return dict(start=start,end=start+wall,wall=wall,cpu=cpu)
windows=[win(1800000000,200000000,5000000),win(2000000000,200000000,5000000)]
b=frame_cpu_bounds(windows,1900000000,2100000000);assert b['complete_window_coverage']and b['lower_ms']==0 and b['upper_ms']==12
busy=[win(1800000000,200000000,199000000),win(2000000000,200000000,199000000)]
b=frame_cpu_bounds(busy,1900000000,2100000000);assert b['complete_window_coverage']and b['lower_ms']==196 and b['upper_ms']==200
assert not frame_cpu_bounds(windows[:1],1900000000,2100000000)['complete_window_coverage']
assert not frame_cpu_bounds(windows+[windows[0]],1900000000,2100000000)['complete_window_coverage']
assert not frame_cpu_bounds([win(1900000000,200000000,202000000)],1900000000,2100000000)['complete_window_coverage']
assert not frame_cpu_bounds([],1900000000,2100000000)['complete_window_coverage']
report=dict(passed=True,authored_trace_fixture=True,scope='offline analyzer contract only, not gameplay or performance',checks=['retained tail interval accepted while whole history explicitly incomplete','hole/busy/status/accounting rejection','drop outside interval does not prove a hole inside it','missing submit span denies coverage','unfinished/nested accounting rejected','invalid/nonmonotonic/saturated/mismatched marker counters rejected','overwritten/out-of-range boundaries rejected','CPU binary hash mismatch rejected','same-thread CPU clipping bounds, missing/overlap/impossible windows rejected'])
(E/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(evidence=str(E),**report)))
