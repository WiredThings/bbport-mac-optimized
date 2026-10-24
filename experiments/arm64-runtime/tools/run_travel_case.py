#!/usr/bin/env python3
"""Private normal-save World Warper validation. No production data or GUI capture.
This is load/lifetime correctness evidence, never a fixed-scene FPS benchmark.
"""
from pathlib import Path
import argparse,csv,hashlib,json,os,re,subprocess,sys,time,traceback
P=Path(__file__).resolve().parents[1]
ROWS={0x15000000:2102950,0x17000000:2302950,0x18010000:2412951}
def atomic(path,text):
 temp=path.with_name(path.name+'.pending');temp.write_text(text);os.replace(temp,path)
def frame_count(path):
 if not path.is_file():return 0
 with path.open()as f:return sum(bool(r.get('cam_x'))and r.get('cam_x')!='cam_x'for r in csv.DictReader(f)if None not in r)
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=240);ap.add_argument('--moves',type=int,default=2);ap.add_argument('--settle-seconds',type=int,default=6);ap.add_argument('--metadata',action='store_true');ap.add_argument('--extra-heaps-fresh',action='store_true');ap.add_argument('--extra-heaps',action='store_true');ap.add_argument('--allocation-audit',action='store_true');ap.add_argument('--inspect-resources',action='store_true');ap.add_argument('--packed-veneers',action='store_true');ap.add_argument('--hot-staging-reuse',action='store_true');a=ap.parse_args();assert not a.inspect_resources or a.metadata;assert 60<=a.seconds<=600 and 1<=a.moves<=6 and 6<=a.settle_seconds<=60
 pad=P/'state'/f'travel-pad-{time.time_ns()}.txt';pad.write_text('')
 hashes={str(p.relative_to(P)):hashlib.sha256(p.read_bytes()).hexdigest()for p in [P/'build/fex/Bin/bb-game-arm64',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib']}
 old=set((P/'evidence').glob('game-*'));console=P/'evidence'/f'travel-case-{time.time_ns()}.log'
 command=[sys.executable,str(P/'tools/run_game.py'),'--native-observer','--dev-world-warper','--resource-hooks','--resource-fast-dispatch','--resource-leaf-dispatch','--boot',str(P.parent/'assets/boot-linked.bin'),'--app0',str(P.parent/'assets/CUSA03173-1.09'),'--content-profile',str(P.parent/'assets/content.bin'),'--seed-user',str(P.parent/'qa-seed-user'),'--clone-private-session',str(P/'state/game-1792762608095377000'),'--gpu-state',str(P/'state/gpu-stutter-fresh-1792770276384734000'),'--graphics','--frame-timing','--pad-file',str(pad),'--seconds',str(a.seconds)]
 if a.packed_veneers:command+=['--packed-veneers']
 if a.hot_staging_reuse:command+=['--hot-staging-reuse']
 if a.extra_heaps:command+=['--extra-heaps']
 if a.extra_heaps_fresh:command+=['--extra-heaps-fresh']
 if a.metadata:command+=['--lifetime-audit']
 if a.allocation_audit:assert a.inspect_resources;command+=['--allocation-audit']
 e=None;state=None;ready=None;armed=None;nonce=None;issued=None;settled=None;snapshot=0;completed=[];original_map=None;error=None;requested=False;finished=False;last_status=None;events=[];inspection_pending=None;inspection_sent=None;inspection_results=[]
 def event(name,**kw):
  row=dict(event=name,monotonic=time.monotonic(),**kw);events.append(row)
  if e:
   with(e/'travel-events.jsonl').open('a')as f:f.write(json.dumps(row)+'\n')
 def finish():
  nonlocal finished
  if state and nonce and not finished:atomic(state/'.private-test-finished',nonce);finished=True
 def request_snapshot():
  nonlocal snapshot,inspection_pending,inspection_sent
  if a.metadata:
   snapshot+=1;atomic(e/'texture-lifetimes/request',str(snapshot)+'\n');event('metadata_requested',sequence=snapshot)
   if a.inspect_resources:inspection_pending=snapshot;inspection_sent=None
 with console.open('w')as out:
  proc=subprocess.Popen(command,stdout=out,stderr=out,cwd=P)
  try:
   while proc.poll()is None:
    now=time.monotonic()
    if e is None:
     fresh=[p for p in(P/'evidence').glob('game-*')if p not in old and(p/'runtime.log').is_file()]
     if len(fresh)==1:
      e=fresh[0];state=P/'state'/e.name;print('Evidence:',e,flush=True)
      (e/'travel-conditions.json').write_text(json.dumps(dict(scope='Shared-only development World Warper, normal save, quicksave off, unchanged quality/cache, resource lifetime+fast+leaf, silent SDL gain zero; no capture/profiling; load correctness, not performance',metadata=a.metadata,hot_staging_reuse=a.hot_staging_reuse,native_observer=True,packed_veneers=a.packed_veneers,extra_heaps=a.extra_heaps,extra_heaps_fresh=a.extra_heaps_fresh,allocation_audit=a.allocation_audit,inspect_resources=a.inspect_resources,moves=a.moves,settle_seconds=a.settle_seconds,seconds=a.seconds,artifact_sha256=hashes),indent=2)+'\n')
    if e and not finished:
     log=(e/'runtime.log').read_text(errors='replace')
     if ready is None and 'character ready; automatic menu input released'in log:ready=now;event('character_ready')
     if ready and armed is None and now-ready>=4 and frame_count(e/'frames.csv')>=30:
      nonce=(state/'.private-world-warper').read_text();assert len(nonce)==32
      atomic(state/'world-warper/armed',nonce);armed=now;event('armed')
     status=state/'world-warper/status.json'
     if armed and status.is_file():
      s=json.loads(status.read_text());last_status=s
      assert s['tool']=='BB_DEVELOPMENT_WORLD_WARPER_FEX_V1'and not s['error'],s
      assert s['stage']!='failed',s
      if original_map is None and s['stage']=='idle'and s['world']and not s['loading']:
       original_map=s['map'];assert original_map in ROWS,('source map not allowlisted for a genuine round trip',hex(original_map));settled=now;request_snapshot();event('source',map=original_map)
      if settled is not None and not requested and inspection_pending is None and now-settled>=a.settle_seconds:
       if len(completed)==a.moves:
        finish();event('completed');continue
       target=0x15000000 if s['map']!=0x15000000 else original_map
       if target==s['map']:target=0x18010000
       sequence=len(completed)+1;issued=dict(sequence=sequence,source=s['map'],destination_map=target,row=ROWS[target],frame_count=frame_count(e/'frames.csv'),began=now)
       atomic(state/'world-warper/command.txt',f'{sequence} {ROWS[target]}\n');event('requested',**issued);requested=True;settled=None
      if requested and s['stage']=='arrived'and s['sequence']==issued['sequence']:
       assert s['saw_loading']and s['saw_world_down']and s['world']and not s['loading']and s['map']==issued['destination_map']and s['stable_ticks']>=30,s
       count=frame_count(e/'frames.csv');assert count-issued['frame_count']>=30,'no new rendered-world frames'
       completed.append(dict(**issued,seconds=now-issued['began'],rendered_world_frames=count-issued['frame_count'],status=s));event('arrived',sequence=s['sequence'],map=s['map']);print(json.dumps(completed[-1]),flush=True);settled=now;requested=False;request_snapshot()
     if inspection_pending is not None:
      source=e/'texture-lifetimes'/f'textures-{inspection_pending}.csv'
      if inspection_sent is None and source.is_file():
       rows=list(csv.DictReader(source.open()));assert rows and all(None not in r and len(r)==40 for r in rows)
       unknown=[r for r in rows if int(r['usage'])&1 and not(int(r['usage'])&12) and not(int(r['current_owner'])or int(r['heap_live_overlaps'])or int(r['range_live_overlaps']))]
       controls=[r for r in rows if int(r['current_owner'])]
       selected=sorted(unknown,key=lambda r:int(r['host_bytes']),reverse=True)[:16]+sorted(controls,key=lambda r:int(r['host_bytes']),reverse=True)[:4]
       assert selected,'no query candidates or controls'
       (e/f'resource-selection-{inspection_pending}.json').write_text(json.dumps(selected,indent=2)+'\n')
       text=f'ARMRESOURCE1 {nonce} {inspection_pending} {len(selected)}\n'+''.join(f"{r['address']} {r['guest_bytes']}\n"for r in selected)
       atomic(state/'world-warper/inspect-request.txt',text);inspection_sent=now;event('cpu_resource_inspection_requested',sequence=inspection_pending,count=len(selected))
      result=state/'world-warper'/f'inspection-{inspection_pending}.json'
      if inspection_sent is not None and result.is_file():
       answer=json.loads(result.read_text());assert answer['sequence']==inspection_pending and answer['nonce']==nonce
       selected=json.loads((e/f'resource-selection-{inspection_pending}.json').read_text());assert [(q['address'],q['size'])for q in answer['queries']]==[(int(r['address']),int(r['guest_bytes']))for r in selected]
       inspection_results.append(str(result));event('cpu_resource_inspection_received',sequence=inspection_pending,count=len(answer['queries']));inspection_pending=None;inspection_sent=None
      elif inspection_sent is not None and now-inspection_sent>20:raise RuntimeError('bounded CPU resource inspection timed out')
     if armed and not status.exists()and now-armed>20:raise RuntimeError('Flipper hook did not publish a status')
    time.sleep(.1)
  except Exception as exc:
   error=f'{type(exc).__name__}: {exc}';event('controller_error',error=error);finish()
  finally:
   # Keep the live supervisor/guards; never abandon or kill it while game children run.
   if proc.poll()is None:
    finish()
    try:proc.wait(timeout=a.seconds+10)
    except subprocess.TimeoutExpired:raise RuntimeError('supervisor did not honor its bounded budget; inspect existing PID, do not restart')
 if e:
  runtime=json.loads((e/'result.json').read_text());log=(e/'runtime.log').read_text(errors='replace')
  snapshots=[str(p)for p in(e/'texture-lifetimes').glob('textures-*.csv')]
  passed=not error and len(completed)==a.moves and finished and runtime['stopped']=='private test completed'and runtime['exit']==0 and not runtime['cleanup_warnings']and 'ARM FATAL'not in log and 'ARM HLE STOP'not in log and 'ARM RESOURCE invalid metadata'not in log and (not a.metadata or len(snapshots)==a.moves+1)and(not a.inspect_resources or len(inspection_results)==a.moves+1)
  cpu_policy=json.loads((e/'cpu-policy.json').read_text());layout_rows=re.findall(r'ARM VENEER layout=([^ ]+)',log)
  expected_layout='darwin-packed-guest-ro-v1' if a.packed_veneers else 'standalone-native-rx-v1'
  layout_verified=cpu_policy['packed_veneers']==a.packed_veneers and bool(layout_rows) and all(row==expected_layout for row in layout_rows)
  gpu_policy=json.loads((e/'gpu-experimental-policy.json').read_text());hot_verified=gpu_policy['hot_staging_reuse']==a.hot_staging_reuse and bool(log.count('Metal staging recent reuse: enabled;'))==a.hot_staging_reuse
  passed=passed and layout_verified and hot_verified
  heap_rows=re.findall(r'Guest resource lifetime heap: .*dropped=(\d+)',log)
  coverage=not any(int(n)for n in heap_rows)
  report=dict(passed=passed,hot_staging_reuse=a.hot_staging_reuse,hot_staging_verified=hot_verified,packed_veneers=a.packed_veneers,layout_verified=layout_verified,layout_markers=layout_rows,lifecycle_coverage_passed=coverage,error=error,source_map=original_map,completed=completed,last_status=last_status,snapshots=snapshots,cpu_inspections=inspection_results,supervisor_exit=proc.returncode,runtime_exit=runtime['exit'],stopped=runtime['stopped'],seconds=runtime['seconds'],formal_data_modified=False,performance_claim=False)
  (e/'travel-result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));assert passed,('incomplete isolated travel validation',e)
 else:raise RuntimeError(f'no game evidence; inspect {console}')
if __name__=='__main__':main()
