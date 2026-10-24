#!/usr/bin/env python3
"""Actual metadata capture ABI, no GPU/device/window/game; outputs only fresh QA files."""
from pathlib import Path
import subprocess,os,json,sys,time,argparse
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'tools'));from native_observer import NativeObserver
ap=argparse.ArgumentParser();ap.add_argument('--evidence',type=Path,required=True);a=ap.parse_args();e=a.evidence.resolve();assert e.parent==P/'evidence'
r=e/('selftests-'+str(time.time_ns()));r.mkdir();state=P/'state'/r.name;state.mkdir()
obs=NativeObserver();before=obs.system();assert not before['locked']and before['pressure']!=4
m=P/'vendor/metal_api';binary=r/'capture-metadata-test';lib=P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.texture-candidate.dylib'
cmd=['/usr/bin/clang++','-std=c++23','-arch','arm64','-mmacosx-version-min=26.0','-I'+str(m/'shadps4'),'-I'+str(m/'shim'),str(P/'tests/test_texture_capture.cpp'),str(lib),'-o',str(binary)]
build=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);(r/'build.log').write_text(build.stdout);assert build.returncode==0,build.stdout[-3000:]
results=[]
for mode in ('on','off','invalid-flag','overflow','no-trigger','late-private','unsafe-late'):
 root=r/mode;root.mkdir();gpu=state/mode;gpu.mkdir();env={k:v for k,v in os.environ.items()if not k.startswith('BB_')};env.update(BB_GPU_USER_DIR=str(gpu),BB_CONFIG=str(root/'bbport.ini'),BB_CAPTURE_NATIVE='1',BB_CAPTURE_TRIGGER=str(root/'trigger'),BB_CAPTURE_DIR=str(root));(root/'bbport.ini').write_text('')
 if mode in ('on','overflow','late-private','unsafe-late'):env['BB_CAPTURE_TEXTURE_BINDINGS']='1'
 elif mode=='invalid-flag':env['BB_CAPTURE_TEXTURE_BINDINGS']='true'
 output=root
 if mode in('late-private','unsafe-late'):
  for k in ('BB_CAPTURE_NATIVE','BB_CAPTURE_TRIGGER','BB_CAPTURE_DIR'):env.pop(k)
  env['BB_TEXTURE_BINDING_CAPTURE_DIR']=str(gpu if mode=='late-private'else root)
  if mode=='late-private':output=gpu
 run=subprocess.run([str(binary),mode],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=15);(root/'console.log').write_text(run.stdout)
 assert run.returncode==0 and 'AUTHORED_TEXTURE_CAPTURE_PASS'in run.stdout,(mode,run.returncode,run.stdout[-1500:])
 files=list(output.glob('frame_*.txt'))
 if mode in('no-trigger','unsafe-late'):assert not files
 else:
  assert len(files)==1;events=[json.loads(x[7:])for x in files[0].read_text().splitlines()if x.startswith('NATIVE ')];header=next(x for x in events if x['op']=='frame')
  assert header['truncated']==(mode=='overflow');markers=[x for x in events if x['op']=='texture_capture'];assert bool(markers)==(mode in('on','overflow','late-private'))
  if markers:assert all(markers[0].get(k)is True for k in('keep_set_memo','keep_render_memo','keep_vertex_memo','keep_bind_helper'))
  assert any(x['op']in('texture_binding','texture_binding_batch')for x in events)
  assert any(x['op']=='memory_live'for x in events)==(not bool(markers))
  assert any(x['op']=='texture_lifetime'for x in events)==(not bool(markers))
  batches=[x for x in events if x['op']=='texture_binding_batch']
  assert bool(batches)==bool(markers)
  if batches:assert any(any('record'in v for v in x['bindings'])for x in batches)
  assert header['bytes']<=2*1024*1024 and len(events)<=8193
 results.append({'mode':mode,'exit':run.returncode,'passed':True,'files':len(files),'real_game':False,'device_created':False,'window_created':False})
parsed=subprocess.run([sys.executable,str(P/'tests/test_texture_bindings_analysis.py')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);(r/'parser-tests.log').write_text(parsed.stdout);assert parsed.returncode==0
report={'before':before,'after':obs.system(),'results':results,'parser_tests':14,'gpu_fixture_state':str(state),'library':str(lib),'evidence':str(r),'real_texture_cache_binding_execution':'not covered; needs live game','real_rendering_correctness_proven':False}
(r/'result.json').write_text(json.dumps(report,indent=2)+'\n');obs.close();print(json.dumps({'evidence':str(r),'actual_capture_modes_passed':len(results),'parser_tests':14,'real_game':False},indent=2))
