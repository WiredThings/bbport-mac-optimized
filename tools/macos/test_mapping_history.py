#!/usr/bin/env python3
"""Check historical physical-page aliases without a game or graphics device."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path)
    p.add_argument('--baseline-source',type=Path,help='Optional earlier runtime source: proves the old history gap rather than validating current safety')
    args=p.parse_args();root=Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='bb-mapping-history-') as tmp:
        exe=Path(tmp)/'history'
        cmd=['clang','-arch','x86_64','-std=gnu11','-O1','-g','-pthread','-I'+str(root/'src')]
        if args.baseline_source:
            cmd+=['-DMEMORY_SOURCE="'+str(args.baseline_source.resolve())+'"','-DRECLAIM=runtime_memory_reclaim_if_unaliased','-DEXPECT_HISTORY=0']
        cmd += [str(root/'tests/test_mapping_history.c'),str(root/'src/low_heap.c'),'-o',str(exe)]
        subprocess.run(cmd,check=True)
        subprocess.run(['codesign','--force','--sign','-',str(exe)],check=True)
        subprocess.run(['codesign','--verify','--strict',str(exe)],check=True)
        results=[]
        for mb,mode in [(5056,'on'),(16384,'on'),(5056,'off'),(5056,'late'),(5056,'oom')]:
            run=subprocess.run([str(exe),mode],env=dict(os.environ,BB_DMEM_MB=str(mb)),capture_output=True,text=True,timeout=15)
            if run.returncode:raise RuntimeError(run.stdout+run.stderr)
            rows=[json.loads(s[8:]) for s in run.stdout.splitlines() if s.startswith('HISTORY ')]
            assert len(rows)==1;results.append(dict(pool_mib=mb,**rows[0]))
        result={'baseline_counterexample':bool(args.baseline_source),'scope':'Actual runtime mappings, synthetic ownership, no GPU or real-game conclusion','cases':results}
        if args.output:args.output.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2))
if __name__=='__main__':main()
