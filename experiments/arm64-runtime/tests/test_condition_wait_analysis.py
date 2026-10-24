#!/usr/bin/env python3
"""Authored selected-condition trace validation, never gameplay evidence."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys,time
P=Path(__file__).resolve().parents[1];e=P/'evidence'/f'authored-condition-trace-{time.time_ns()}';e.mkdir()
art=[P/'build/fex/Bin/bb-game-arm64',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib'];hashes={str(f):hashlib.file_digest(f.open('rb'),'sha256').hexdigest()for f in art}
(e/'guest-frame-artifacts.json').write_text(json.dumps(hashes));(e/'runtime.log').write_text('BACHATA_FEX_HLE_BEGIN operation=21 name=runtime.cond_wait\n');(e/'result.json').write_text(json.dumps(dict(exit=0,stopped='time budget',authored_trace_fixture=True)))
# Same-thread closed reports bracket a100ms wait; another thread signals at60ms.
rows=[(56,10,1000000000,0,0,10|(10<<32)),(57,10,1000000000,0,0,0),(54,11,2060000000,1000000,1000,7|(2<<30)),(50,10,2000000000,100000000,1000000,7),(51,10,2000000000,0,0,0x4000),(52,10,2000000000,0,0,0x5000),(53,10,2000000000,0,0,0),(55,10,2000000000,0,0,21),(40,12,2000000000,100000000,0,1),(56,10,3000000000,0,0,12|(12<<32)),(57,10,3000000000,0,0,0)]
data=bytearray(64+16384*64);struct.pack_into('<8s7Q',data,0,b'BBAUDIT1',1,123,1,1,16384,len(rows),0)
for i,(k,tid,t,w,c,a)in enumerate(rows):struct.pack_into('<QII6Q',data,64+i*64,i+1,0,k,tid,t,w,c,a,0)
def run(blob=data):
 (e/'stutter-123.bin').write_bytes(blob);r=subprocess.run([sys.executable,str(P/'tools/analyze_condition_waits.py'),str(e)],capture_output=True,text=True);assert r.returncode==0,r.stderr;return json.loads((e/'condition-wait-summary.json').read_text())
s=run();assert s['authored_trace_fixture'];w=s['waits'][0];assert w['coverage']['valid']and w['condition_slot']==0x4000 and w['mutex_slot']==0x5000 and w['result']==0 and w['wakes'][0]['result']==0 and w['wakes'][0]['wait_return_after_api_end_ms']==39 and s['long_frames'][0]['condition_wait_union_ms']==100
for index in [2,4,5,6,7]:
 bad=bytearray(data);struct.pack_into('<I',bad,64+index*64+8,1);s=run(bad);assert not s['waits'][0].get('coverage',{}).get('valid')
bad=bytearray(data);struct.pack_into('<Q',bad,64+10*64+48,1);s=run(bad);assert not s['waits'][0]['coverage']['valid'] # error counter changed
bad=bytearray(data);struct.pack_into('<Q',bad,64+9*64+48,12|(11<<32));s=run(bad);assert not s['waits'][0]['coverage']['valid']
bad=bytearray(data);struct.pack_into('<Q',bad,64+2*64+48,7|(2<<30)|(22<<32));s=run(bad);assert s['waits'][0]['wakes'][0]['result']==22 # failure must remain visible
bad=bytearray(data);struct.pack_into('<Q',bad,64+2*64+48,8|(2<<30));s=run(bad);assert not s['waits'][0]['wakes'] # another token cannot be attributed
bad=bytearray(data);struct.pack_into('<Q',bad,64+2*64+48,7|(1<<30));s=run(bad);assert not s['waits'][0]['wakes'][0]['valid']
# Producer window spans the whole wait-to-notification interval. Do not prorate CPU.
more=bytearray(data);struct.pack_into('<Q',more,48,len(rows)+1)
struct.pack_into('<QII6Q',more,64+len(rows)*64,len(rows)+1,0,58,11,1900000000,161000000,2000000,0x4000,0)
s=run(more);b=s['waits'][0]['wakes'][0]['producer_cpu_before_notification'];assert b['complete_window_coverage']and b['lower_ms']==0 and b['upper_ms']==3
struct.pack_into('<Q',more,64+len(rows)*64+40,160000000);s=run(more);b=s['waits'][0]['wakes'][0]['producer_cpu_before_notification'];assert b['complete_window_coverage']and b['lower_ms']==59 and b['upper_ms']==61
struct.pack_into('<Q',more,64+len(rows)*64+48,0x6000);s=run(more);assert not s['waits'][0]['wakes'][0]['producer_cpu_before_notification']['complete_window_coverage']
wrong=dict(hashes);wrong[str(art[0])]='0'*64;(e/'guest-frame-artifacts.json').write_text(json.dumps(wrong));r=subprocess.run([sys.executable,str(P/'tools/analyze_condition_waits.py'),str(e)],capture_output=True,text=True);assert r.returncode!=0 and 'artifact changed'in r.stderr
(e/'guest-frame-artifacts.json').write_text(json.dumps(hashes));run();report=dict(passed=True,authored_trace_fixture=True,scope='offline metadata/hash/counter tests only, not game or performance',checks=['wait-slot and mutex metadata','same-token matching signal and return result','relative wake delay','missing/busy records deny complete interval','error/outstanding-call counters deny coverage','wrong token is not a matching wake','invalid stage and nonzero return stay visible','binary hash mismatch refused','matching producer CPU windows and opaque condition identity bounds']);(e/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(evidence=str(e),**report)))
