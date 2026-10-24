#!/usr/bin/env python3
"""A single known PSO archive versus normal creation; no game, GUI or formal cache writes."""
from pathlib import Path
import json,subprocess,sys,time,hashlib
from run_game import system
P=Path(__file__).resolve().parents[1]
ENV={'PATH':'/usr/bin:/bin:/usr/sbin:/sbin','HOME':str(P/'state'),'TMPDIR':str(P/'state')+'/'}
def main():
 start=system();assert not start['locked'] and start['pressure']!=4
 evidence=P/'evidence/disk-pso-probe'/str(time.time_ns());evidence.mkdir()
 source=P/'state/gpu-stutter-fresh-1792770276384734000/cache/CUSA03173/metal/000000002da7fe60_compute_cad8b4995ac37ce0.metal'
 rows=[]
 for label,shader in [('synthetic','-'),('actual-2da7fe60',str(source))]:
  archive=evidence/(label+'.binaryarchive')
  for index,mode in enumerate(('seed','none','load','load','none','none','load')):
   check=system();assert not check['locked'] and check['pressure']!=4 and check['swap_mib']-start['swap_mib']<=256
   cmd=[str(P/'build/probe-disk-pso'),mode,shader,str(archive),'32']
   run=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30,text=True,env=ENV)
   (evidence/f'{label}-{index}-{mode}.stdout').write_text(run.stdout)
   (evidence/f'{label}-{index}-{mode}.stderr').write_text(run.stderr)
   assert run.returncode==0,(label,mode,run.returncode,run.stderr)
   row=json.loads(run.stdout);row.update(label=label,index=index,archive_bytes=archive.stat().st_size,system=system());rows.append(row)
   print(label,mode,'library',round(row['library_ms'],3),'pso',round(row['pso_ms'],3),'footprint_mib',round(row['footprint_pso']/2**20,2),flush=True)
 # Identity rejection: shader differs; never compile/run against the wrong archive.
 bad=subprocess.run([str(P/'build/probe-disk-pso'),'load','-',str(evidence/'actual-2da7fe60.binaryarchive'),'32'],capture_output=True,text=True,timeout=10,env=ENV)
 assert bad.returncode==5 and 'identity mismatch' in bad.stderr
 (evidence/'identity-rejection.log').write_text(bad.stderr)
 result=dict(scope='single compute PSO microbenchmark, not gameplay FPS or whole-game memory',source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),os_compiler_cache_cleared=False,actual_shader_execution_verified=False,synthetic_readback_verified=True,identity_mismatch_rejected=True,before=start,after=system(),rows=rows)
 (evidence/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('Evidence:',evidence,flush=True)
if __name__=='__main__':main()
