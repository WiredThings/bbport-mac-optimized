#!/usr/bin/env python3
"""Prove the physical-alias limit of the released virtual-range predicate.

CPU-only diagnostic using actual runtime mappings, the production NID lookup and
actual Tracker implementation. No game, graphics device, saves or caches are used.
A positive virtual proof with a live physical alias is the expected counterexample;
it does not establish any real-game alias, texture loss or permission to reclaim.
"""
from pathlib import Path
import argparse
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def verify(out):
    out.mkdir(parents=True, exist_ok=False)
    memory_test = (ROOT / 'tests/test_runtime_memory_protection.c').read_text()
    prefix = memory_test.split('static void handler(')[0]
    runtime = (ROOT / 'src/runtime.c').read_text()
    lookup = runtime[runtime.index('static const struct { const char *nid, *symbol; } import_names[]='):runtime.index('const char *runtime_import_name(')]
    stub = 'uintptr_t runtime_lookup(const RuntimeExport* t,size_t n,const char* s){for(size_t i=0;i<n;++i)if(!strcmp(t[i].name,s))return (uintptr_t)t[i].function;return 0;}'
    assert prefix.count(stub) == 1
    source = prefix.replace(stub, lookup) + HELPER
    c = out / 'runtime_alias.c'
    c.write_text(source)
    common = ['-arch', 'x86_64', '-O1', '-g', '-pthread', '-I'+str(ROOT/'src')]
    memory_obj, heap_obj = out/'runtime_alias.o', out/'low_heap.o'
    subprocess.run(['clang', *common, '-std=gnu11', '-DMEMORY_SOURCE="'+str(ROOT/'src/runtime_memory.c')+'"', '-c', str(c), '-o', str(memory_obj)], check=True)
    subprocess.run(['clang', *common, '-std=gnu11', '-c', str(ROOT/'src/low_heap.c'), '-o', str(heap_obj)], check=True)
    binary = out/'retirement-alias-test'
    subprocess.run(['clang++', *common, '-std=c++23', '-I'+str(ROOT/'engines/metal/gpu/shim'),
                    str(ROOT/'tests/test_retirement_alias.cpp'), str(memory_obj), str(heap_obj),
                    str(ROOT/'engines/metal/gpu/shim/bbport_resource_lifetime.cpp'), '-o', str(binary)], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(binary)], check=True)
    subprocess.run(['codesign', '--verify', '--strict', str(binary)], check=True)
    run = subprocess.run([str(binary)], text=True, capture_output=True, timeout=10)
    (out/'run.log').write_text(run.stdout+run.stderr)
    run.check_returncode()
    lines = [line[8:] for line in run.stdout.splitlines() if line.startswith('ALIAS95 ')]
    assert len(lines) == 1
    result = json.loads(lines[0])
    assert result['virtual_a'] != result['virtual_b'] and result['physical_a'] == result['physical_b']
    assert result['live_alias'] and result['cpu_shared_bytes_verified']
    assert result['virtual_preflight_permits'] and result['virtual_transaction_permits']
    assert not result['actual_texture_freed']
    assert result['mapping_transaction_refused_live_alias']
    assert result['mapping_history_refused_unmapped_live_owner']
    result['scope'] = 'Actual runtime + Tracker, synthetic ownership. Virtual proof alone excludes no physical aliases. No GPU or real-game alias conclusion.'
    (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))

HELPER = r"""
void alias95_create(uintptr_t* a,uintptr_t* b,uint64_t* pa,uint64_t* pb) {
 setenv("BB_TEXTURE_RELEASED_STORAGE","1",1);
 int64_t phys;assert(!direct_allocate(0,POOL_SIZE,PAGE*2,PAGE,3,&phys));
 void* first=NULL;void* other=NULL;assert(!direct_map(&first,PAGE*2,0x33,0,phys,0));assert(!direct_map(&other,PAGE*2,0x33,0,phys,0));
 VirtualQueryInfo x={0},y={0};
 typedef int32_t(*Q)(const void*,int,VirtualQueryInfo*,uint64_t);
 Q query=(Q)runtime_memory_resolve("rVjRvHJ0X6c#p#J");assert(query);
 assert(!query(first,0,&x,sizeof(x)) && !query(other,0,&y,sizeof(y)));
 assert(x.offset==y.offset && first!=other);*a=(uintptr_t)first;*b=(uintptr_t)other;*pa=x.offset;*pb=y.offset;
 ((volatile unsigned char*)first)[0]=0xa5;assert(((volatile unsigned char*)other)[0]==0xa5);
 ((volatile unsigned char*)other)[0]=0x6b;assert(((volatile unsigned char*)first)[0]==0x6b);
}
void alias95_unmap(uintptr_t address) {assert(!direct_unmap((void*)address,PAGE*2));}

"""

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, help='New evidence directory; existing directories are not overwritten')
    args = parser.parse_args()
    if args.output:
        verify(args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='bb-retirement-alias-') as path:
            verify(Path(path)/'evidence')

if __name__ == '__main__':
    main()
