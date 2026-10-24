#!/usr/bin/env python3
"""Repeat blood-bullet/heal actions only in a cloned fixed QA checkpoint, no GUI capture."""
from pathlib import Path
import argparse,json,subprocess,time,sys,os
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--extra-heaps-fresh',action='store_true');ap.add_argument('--extra-heaps',action='store_true');ap.add_argument('--resource-hooks',action='store_true');ap.add_argument('--resource-fast-dispatch',action='store_true');ap.add_argument('--resource-leaf-dispatch',action='store_true');ap.add_argument('--engine',choices=['arm64','rosetta'],default='arm64');ap.add_argument('--gpu-state',type=Path);ap.add_argument('--fresh-gpu-cache',action='store_true');ap.add_argument('--audit',action='store_true');ap.add_argument('--audit-start-delay',type=float,default=6.0);ap.add_argument('--coarse-audit',action='store_true');ap.add_argument('--native-observer',action='store_true');ap.add_argument('--slow-packet-audit',action='store_true');ap.add_argument('--guest-frame-audit',action='store_true');ap.add_argument('--guest-sync-audit',action='store_true');ap.add_argument('--pipeline-archive',action='store_true');ap.add_argument('--compile-audit',action='store_true');ap.add_argument('--staging-audit',action='store_true');ap.add_argument('--hot-staging-reuse',action='store_true');ap.add_argument('--capture-trigger',type=Path);ap.add_argument('--seconds',type=int,default=150);ap.add_argument('--cycles',type=int,default=2);ap.add_argument('--sample-cpu',action='store_true');ap.add_argument('--fex-mapping-audit',action='store_true');ap.add_argument('--cpu-code-map',action='store_true');ap.add_argument('--cpu-cache-control',action='store_true');ap.add_argument('--jit-reserve-mib',type=int,choices=[16,128],default=16);ap.add_argument('--cpu-cache',type=Path);ap.add_argument('--packed-veneers',action='store_true');ap.add_argument('--fast-hle-lookup',action='store_true');ap.add_argument('--legacy-hle-lookup',action='store_true');a=ap.parse_args()
 a.audit=a.audit or a.coarse_audit
 assert 0<=a.audit_start_delay<=max(0,a.seconds-10)
 assert a.audit_start_delay==6.0 or a.coarse_audit,'custom audit delay requires coarse audit'
 assert not(a.gpu_state and a.fresh_gpu_cache)
 assert not(a.fast_hle_lookup and a.legacy_hle_lookup)
 use_fast=a.engine=='arm64' and not a.legacy_hle_lookup
 assert 1<=a.cycles<=8
 assert not a.sample_cpu or (a.audit and a.engine=="arm64"),"CPU sampling is diagnostic ARM-only"
 gpu=a.gpu_state or (P/'state'/f'gpu-stutter-fresh-{time.time_ns()}' if a.fresh_gpu_cache else P/'state/gpu-1792757530237447000')
 pad=P/'state'/f'stutter-pad-{time.time_ns()}.txt';pad.write_text('')
 old=set((P/'evidence').glob('game-*'))
 cmd=[sys.executable,str(P/'tools/run_game.py'),'--boot',str(P.parent/'assets/boot-linked.bin'),'--app0',str(P.parent/'assets/CUSA03173-1.09'),'--content-profile',str(P.parent/'assets/content.bin'),'--seed-user',str(P.parent/'qa-seed-user'),'--clone-private-session',str(P/'state/game-1792762608095377000'),'--gpu-state',str(gpu),'--graphics','--quicksave','--frame-timing','--pad-file',str(pad),'--seconds',str(a.seconds)]
 cmd+=['--engine',a.engine]
 if a.fex_mapping_audit:cmd+=['--fex-mapping-audit']
 if a.cpu_code_map:cmd+=['--cpu-code-map']
 if a.packed_veneers:cmd+=['--packed-veneers']
 if a.cpu_cache_control:cmd+=['--cpu-cache-control']
 if a.jit_reserve_mib!=16:cmd+=['--jit-reserve-mib',str(a.jit_reserve_mib)]
 if a.cpu_cache:cmd+=['--cpu-cache',str(a.cpu_cache)]
 if a.extra_heaps:cmd+=['--extra-heaps']
 if a.extra_heaps_fresh:cmd+=['--extra-heaps-fresh']
 if a.resource_hooks:cmd+=['--resource-hooks']
 if a.resource_fast_dispatch:cmd+=['--resource-fast-dispatch']
 if a.resource_leaf_dispatch:cmd+=['--resource-leaf-dispatch']
 if a.audit:cmd+=['--stutter-audit']
 if a.coarse_audit:cmd+=['--coarse-audit']
 if a.native_observer:cmd+=['--native-observer']
 if a.slow_packet_audit:cmd+=['--slow-packet-audit']
 if a.guest_frame_audit:cmd+=['--guest-frame-audit']
 if a.guest_sync_audit:cmd+=['--guest-sync-audit']
 if a.fast_hle_lookup:cmd+=['--fast-hle-lookup']
 if a.legacy_hle_lookup:cmd+=['--legacy-hle-lookup']
 if a.pipeline_archive:cmd+=['--pipeline-archive']
 if a.compile_audit:cmd+=['--compile-audit']
 if a.staging_audit:cmd+=['--staging-audit']
 if a.hot_staging_reuse:cmd+=['--hot-staging-reuse']
 if a.capture_trigger:cmd+=['--capture-trigger',str(a.capture_trigger)]
 console=P/'evidence'/f'stutter-case-{time.time_ns()}.log'
 with console.open('w') as out:
  proc=subprocess.Popen(cmd,stdout=out,stderr=out,cwd=P)
  e=None;ready=None;cycle=0;phase=0;next_action=None;gate_open=False;gate_closed=False;last_cycle_end=None;sampler=None;sample_log=None
  def event(name,**kw):
   row=dict(event=name,mono_ns=time.clock_gettime_ns(time.CLOCK_MONOTONIC),utc_ns=time.time_ns(),**kw)
   with (e/'operator-events.jsonl').open('a') as f:f.write(json.dumps(row)+'\n')
  def input_(text):
   # File is unique to this run. Atomic replacement avoids partial tokens.
   temp=pad.with_suffix('.tmp');temp.write_text(text+'\n');os.replace(temp,pad)
  while proc.poll() is None:
   if e is None:
    fresh=[p for p in (P/'evidence').glob('game-*') if p not in old and (p/'runtime.log').exists()]
    if len(fresh)==1:
     e=fresh[0];print('Evidence:',e,flush=True)
     (e/'stutter-case-conditions.json').write_text(json.dumps({'gpu_state':str(gpu),'fresh_private_gpu_cache':a.fresh_gpu_cache,'audit':a.audit,'audit_start_delay_s':a.audit_start_delay,'coarse_audit':a.coarse_audit,'native_observer':a.native_observer,'slow_packet_audit':a.slow_packet_audit,'guest_frame_audit':a.guest_frame_audit,'guest_sync_audit':a.guest_sync_audit,'generic_condition_wait_scopes_suppressed':a.guest_sync_audit,'producer_cpu_window':a.guest_sync_audit,'pipeline_archive':a.pipeline_archive,'compile_audit':a.compile_audit,'staging_audit':a.staging_audit,'hot_staging_reuse':a.hot_staging_reuse,'native_frame_capture':bool(a.capture_trigger),'os_metal_cache_cleared':False,'engine':a.engine,'cycles':a.cycles,'sample_cpu':a.sample_cpu,'fex_mapping_audit':a.fex_mapping_audit,'cpu_code_map':a.cpu_code_map,'cpu_cache_control':a.cpu_cache_control,'packed_veneers':a.packed_veneers,'jit_initial_reserve_mib':a.jit_reserve_mib,'cpu_cache':str(a.cpu_cache)if a.cpu_cache else None,'code_map_frames_not_performance_evidence':a.cpu_code_map,'resource_hooks':a.resource_hooks,'extra_heaps':a.extra_heaps,'extra_heaps_fresh':a.extra_heaps_fresh,'resource_fast_dispatch':a.resource_fast_dispatch,'resource_leaf_dispatch':a.resource_leaf_dispatch,'fast_hle_lookup':use_fast,'cpu_sample_interval_ms':10 if a.sample_cpu else None},indent=2)+'\n')
   if e:
    log=(e/'runtime.log').read_text(errors='replace') if ready is None else ''
    if ready is None and 'character ready; automatic menu input released' in log:
     ready=time.monotonic();next_action=ready+12;event('character_ready')
    now=time.monotonic()
    if a.audit and ready is not None:
     if not gate_open and now>=ready+a.audit_start_delay:
      with (e/'stutter-control.bin').open('r+b') as f:f.write(b'\x01\x00\x00\x00')
      gate_open=True;event('audit_gate_open')
     if not a.coarse_audit and last_cycle_end is not None and now>=last_cycle_end+8 and not gate_closed:
      with (e/'stutter-control.bin').open('r+b') as f:f.write(b'\x00\x00\x00\x00')
      gate_closed=True;event('audit_gate_close')
    if a.sample_cpu and ready is not None and sampler is None and now>=ready+40:
     table=subprocess.check_output(['/bin/ps','-axo','pid,ppid,comm'],text=True)
     owned=[int(f[0]) for line in table.splitlines()[1:] if len(f:=line.strip().split(None,2))==3 and int(f[1])==proc.pid and f[2]==str(P/'build/fex/Bin/bb-game-arm64')]
     if len(owned)!=1:raise RuntimeError('CPU diagnostic target is ambiguous')
     sample_log=(e/'cpu-sample-console.log').open('w')
     sampler=subprocess.Popen(['/usr/bin/sample',str(owned[0]),'60','10','-file',str(e/'cpu.sample.txt')],stdout=sample_log,stderr=sample_log)
     event('cpu_sample_started',target_pid=owned[0],sampler_pid=sampler.pid,duration_s=60,interval_ms=10)
    if next_action is not None and now>=next_action:
     # Identical requested cycles, no movement. L2 frees one blood-bullet slot first.
     steps=[('l2',.18),('',2.0),('up',.18),('',2.5),('triangle',.18),('',0)]
     if phase==0:event('cycle_start',cycle=cycle+1)
     text,delay=steps[phase];input_(text);event('input',cycle=cycle+1,token=text)
     phase+=1
     if phase==len(steps):
      cycle+=1;phase=0;next_action=ready+12+cycle*15 if cycle<a.cycles else None;last_cycle_end=now if cycle==a.cycles else None;event('cycle_end',cycle=cycle)
     else:next_action=now+delay
   time.sleep(.05)
  if sampler is not None:
   if sampler.poll() is None:
    sampler.terminate()
    try:sampler.wait(timeout=3)
    except subprocess.TimeoutExpired:sampler.kill();sampler.wait()
   sample_log.close()
   event('cpu_sample_finished',exit=sampler.returncode)
  print('supervisor_exit' ,proc.returncode,'console',console,flush=True)
  if e and a.audit:subprocess.run([sys.executable,str(P/'tools/analyze_stutter.py'),str(e)],check=True)
  if e:
   r=json.loads((e/'result.json').read_text());print(json.dumps({k:r[k] for k in ('exit','stopped','seconds','cleanup_warnings')},indent=2))
   if r['exit']!=0 or r['stopped']!='time budget' or cycle!=a.cycles:raise RuntimeError('Incomplete isolated action case; not a passed test')
  if proc.returncode:sys.exit(proc.returncode)
if __name__=='__main__':main()
