#!/usr/bin/env python3
"""One owned Shared CPU/GPU kernel snapshot, never performance evidence."""
from pathlib import Path
import argparse,json,subprocess,sys,time
from native_observer import NativeObserver
from memory_categories import parse
from fex_map_provenance import associate
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('case',type=Path);ap.add_argument('cpu',type=int);ap.add_argument('gpu',type=int);ap.add_argument('--fex-mapping-audit',action='store_true');a=ap.parse_args()
 case=a.case.resolve();cpu=a.cpu;gpu=a.gpu;assert case.is_relative_to(P/'evidence')and case.is_dir()and cpu!=gpu and cpu>0 and gpu>0
 obs=NativeObserver();cpu_path=obs.path(cpu);gpu_path=obs.path(gpu);assert cpu_path in(str(P/'build/fex/Bin/bb-game-arm64'),str(P/'rosetta-reference/bin/bb-probe-metal'));cohort=json.loads((case.parent/'result.json').read_text())
 expected_gpu=P/'backend/bbport/bin/Bloodborne Metal.app/Contents/MacOS/bb-gpu'
 if cohort.get('gpu_variant')=='first-upload':
  candidate=json.loads((P/'evidence/first-upload-gpu-app.json').read_text());expected_gpu=Path(candidate['target'])
  assert expected_gpu.is_relative_to(P/'backend/bbport/bin')and expected_gpu.name=='bb-gpu'
 assert gpu_path==str(expected_gpu)
 identities={cpu:cpu_path,gpu:gpu_path};mapping_before=None;runtime=None
 if a.fex_mapping_audit:
  assert cpu_path==str(P/'build/fex/Bin/bb-game-arm64'),'FEX mapping audit requires native CPU'
  conditions=json.loads((case/'conditions.json').read_text());assert conditions['env'].get('BB_FEX_MAPPING_AUDIT')=='1'
  data=Path(conditions['data']).resolve();assert data.is_relative_to(P/'state')
  runtime=data/'out/last-run.log';assert runtime.is_file()and not runtime.is_symlink()
  mapping_before=runtime.read_text();(case/'memory.mapping-before.log').write_text(mapping_before)
 before=obs.system();assert not before['locked']and before['pressure']!=4
 start=time.monotonic();output=case/'memory.footprint.json';cmd=['/usr/bin/footprint','-j',str(output),'-f','bytes','--swapped','--wired','-p',str(cpu),'-p',str(gpu)]
 with(case/'memory.footprint.log').open('w')as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=15)
 parsed=parse(json.loads(output.read_text()),{cpu:'game',gpu:'gpu'})
 provenance=None
 if a.fex_mapping_audit:
  assert all(obs.path(pid)==path for pid,path in identities.items()),'target identity changed before regions'
  regions=case/'memory.regions.txt'
  with regions.open('w')as log:subprocess.run(['/usr/bin/vmmap','-w','-pages','-interleaved','-noCoalesce',str(cpu)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=15)
  kernel_end=time.clock_gettime_ns(time.CLOCK_MONOTONIC);deadline=time.monotonic()+3
  while True:
   mapping_after=runtime.read_text()
   try:provenance=associate(mapping_before,mapping_after,regions.read_text(),cpu,required_after_mono_ns=kernel_end);break
   except ValueError as exc:
    if str(exc)!='no fresh post-kernel publication checkpoint'or time.monotonic()>=deadline:raise
    time.sleep(.05)
  (case/'memory.mapping-after.log').write_text(mapping_after)
 assert all(obs.path(pid)==path for pid,path in identities.items()),'target identity changed'
 (case/'memory.parsed.json').write_text(json.dumps(dict(parsed=parsed,cost_seconds=time.monotonic()-start,before=before,after=obs.system(),identities=identities,performance_evidence=False,fex_provenance=provenance),indent=2)+'\n');print('Kernel memory snapshot recorded; no performance claim',flush=True)
if __name__=='__main__':main()
