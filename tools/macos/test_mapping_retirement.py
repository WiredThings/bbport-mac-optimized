#!/usr/bin/env python3
"""Test actual runtime mapping-retirement admission without a game or GPU."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,help='Optional JSON result file')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='bb-mapping-retirement-') as path:
        exe=Path(path)/'test'
        subprocess.run(['clang','-arch','x86_64','-std=gnu11','-O1','-g','-pthread',
                        '-I'+str(root/'src'),str(root/'tests/test_mapping_retirement.c'),
                        str(root/'src/low_heap.c'),'-o',str(exe)],check=True)
        subprocess.run(['codesign','--force','--sign','-',str(exe)],check=True)
        subprocess.run(['codesign','--verify','--strict',str(exe)],check=True)
        run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
        run.check_returncode()
        lines=[line.partition(' ')[2] for line in run.stdout.splitlines() if line.startswith('MAPPING_RETIREMENT ')]
        assert len(lines)==1
        result=json.loads(lines[0])
        absent=Path(path)/'missing-runtime-api'
        subprocess.run(['clang++','-arch','x86_64','-std=c++23','-O1',
                        '-Wl,-undefined,dynamic_lookup','-I'+str(root/'engines/metal/gpu/shim'),
                        str(root/'tests/test_reclaim_mapping_absent.cpp'),'-o',str(absent)],check=True)
        subprocess.run(['codesign','--force','--sign','-',str(absent)],check=True)
        subprocess.run(['codesign','--verify','--strict',str(absent)],check=True)
        missing=subprocess.run([str(absent)],capture_output=True,text=True,timeout=5,check=True)
        result['missing_runtime_api_retirement_refused']=True
        result['missing_runtime_api_stdout']=missing.stdout
        result.update(scope='Actual runtime mapping primitive; no cache/GPU integration or texture deletion',stdout=run.stdout)
        if args.output:args.output.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2))

if __name__=='__main__':main()
