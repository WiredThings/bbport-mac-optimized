#!/usr/bin/env python3
"""Actual runtime + TextureCache + Metal retirement, without a game or screen capture.

Uses an existing low-parallelism Metal test build; all generated objects, libraries,
logs and user/config paths live in a new private output directory. The source fixture
is adapted with checked substitutions; the engine test and formal app stay unchanged.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT=Path(__file__).resolve().parents[2]
MODES=('released-storage-off','released-storage','released-storage-alias','released-storage-historical','released-storage-no-history')
def replace(s,old,new):
    assert s.count(old)==1,old[:120]
    return s.replace(old,new)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--build-only',action='store_true')
    p.add_argument('--mode',choices=MODES,action='append',help='Run only the selected native cases; default is all five')
    args=p.parse_args();b=args.build.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=ROOT/'engines/metal/tests/test_texture_cache_views.mm';s=source.read_text()
    s=replace(s,'#define runtime_memory_region unused_memory_region','#define runtime_memory_region unused_memory_region\n#define runtime_memory_generation unused_memory_generation')
    s=replace(s,'#undef runtime_memory_region','#undef runtime_memory_region\n#undef runtime_memory_generation')
    start=s.index('// Controlled mapping model for the cache/Metal integration.')
    end=s.index('using namespace VideoCore;',start)
    s=s[:start]+'#include "test_runtime_mapping_glue.h"\n\n'+s[end:]
    start=s.index('    void* backing=MAP_FAILED;');end=s.index('    if (released_on || released_off) {',start)
    s=s[:start]+'    void* backing=TestMappingInitialize(argv[1]);\n'+s[end:]
    assert s.count('munmap(backing,0x800000);')==2
    s=s.replace('munmap(backing,0x800000);','TestMappingFinish();')
    s=replace(s,'const bool alias_case=argc>1 && std::string_view(argv[1])=="released-storage-alias";',
      'const bool alias_case=argc>1 && (std::string_view(argv[1])=="released-storage-alias" || std::string_view(argv[1])=="released-storage-historical" || std::string_view(argv[1])=="released-storage-no-history");')
    # Real logical B ownership remains live even when its VMA has gone.
    s=replace(s,'    const auto size=desc.info.guest_size;',
      '    const auto size=desc.info.guest_size;\n    if(mapping_alias_address())assert(lifetime.Allocate(Life::Domain::Heap,mapping_alias_address(),size));')
    start=s.index('    if(enabled && test_mapping_alias) {');end=s.index('    if (!enabled) {',start)
    s=s[:start]+'''    if(enabled && test_mapping_alias) {
        assert(f.textures.TryGetImage(image_id,uid)==image && image->IsTracked());
        assert(f.textures.RegistryGeneration()==generation && test_mapping_callbacks==0);
        assert(test_mapping_attempts>0 && lifetime.Read().reclaimed_images==0);
        verify_pixels(image->backing->image.native,true);unchanged_cpu();
        held_view=nil;Life::SetReclaimReady(false);
        std::printf("JOINT100 {\\"retained_exact_pixels\\":true,\\"reclaimed\\":false,\\"attempts\\":%u,\\"callbacks\\":%u,\\"history_bytes\\":%llu}\\n",test_mapping_attempts,test_mapping_callbacks,(unsigned long long)mapping_history_bytes());
        return;
    }
'''+s[end:]
    s=replace(s,'        std::puts("Released storage OFF: actual cache retains the GPU-written resource");',
      '        assert(test_mapping_attempts==0 && !mapping_history_bytes());\n        std::puts("JOINT100 {\\"reclaimed\\":false,\\"candidate_off\\":true,\\"history_bytes\\":0}");')
    needle='    std::puts("Released storage ON: cache untracks, unregisters and destroys native views/backing; address reuse gets a fresh UID; no writeback");'
    s=replace(s,'    held_view=nil;\n    drain_deferred();\n    assert(weak_base==nil);',
      '    const auto native_before_drop=f.device.GetImpl().device.currentAllocatedSize;\n    held_view=nil;\n    drain_deferred();\n    const auto native_after_drop=f.device.GetImpl().device.currentAllocatedSize;\n    assert(weak_base==nil);')
    s=replace(s,needle,'''    assert(test_mapping_callbacks==1 && test_nested_protects>0);
    std::printf("JOINT100 {\\"reclaimed\\":true,\\"native_destroyed\\":true,\\"address_reuse_pixels_exact\\":true,\\"callbacks\\":%u,\\"nested_protects\\":%u,\\"history_bytes\\":%llu,\\"metal_allocated_before_view_drop\\":%llu,\\"metal_allocated_after_view_drop\\":%llu}\\n",test_mapping_callbacks,test_nested_protects,(unsigned long long)mapping_history_bytes(),(unsigned long long)native_before_drop,(unsigned long long)native_after_drop);''')
    generated=out/'cache-runtime.mm';generated.write_text(s)
    commands=subprocess.check_output(['ninja','-t','commands','texture-cache-views-test'],cwd=b,text=True).splitlines()
    cc=shlex.split(next(x for x in commands if str(source) in x and ' -c ' in x))
    arch=cc[cc.index('-arch')+1];oldobj=cc[cc.index('-o')+1];obj=out/'cache-runtime.o'
    cc=[str(obj) if x==oldobj else str(generated) if x==str(source) else x for x in cc]
    cc+=['-I'+str(ROOT/'engines/metal/tests'),'-I'+str(ROOT/'tests')]
    for flag in ('-MF','-MT'):
        if flag in cc:cc[cc.index(flag)+1]=str(out/('cache-runtime.d' if flag=='-MF' else 'cache-runtime.o'))
    library=out/'actual-runtime.dylib'
    with (out/'build.log').open('w') as log:
        subprocess.run(['clang','-arch',arch,'-std=gnu11','-O1','-g','-pthread','-dynamiclib',
          '-I'+str(ROOT/'src'),str(ROOT/'tests/runtime_mapping_bridge.c'),str(ROOT/'src/low_heap.c'),'-o',str(library)],stdout=log,stderr=subprocess.STDOUT,check=True)
        subprocess.run(cc,cwd=b,stdout=log,stderr=subprocess.STDOUT,check=True)
        line=next(x for x in commands if ' -o texture-cache-views-test ' in x);link=shlex.split(line);link=link[link.index('&&')+1:];link=link[:link.index('&&')]
        exe=out/'cache-runtime-test';link=[str(obj) if x==oldobj else str(exe) if x=='texture-cache-views-test' else x for x in link]
        subprocess.run(link,cwd=b,stdout=log,stderr=subprocess.STDOUT,check=True)
    for f in (library,exe):
        subprocess.run(['codesign','--force','--sign','-',str(f)],check=True)
    for f in (library,exe,b/'libbbgpu.dylib'):
        subprocess.run(['codesign','--verify','--strict',str(f)],check=True)
    report={'status':'running','architecture':arch,'actual_runtime_and_metal':True,'generated_source_sha256':hashlib.sha256(generated.read_bytes()).hexdigest(),
            'runtime_source_sha256':hashlib.sha256((ROOT/'src/runtime_memory.c').read_bytes()).hexdigest(),'runs':[],
            'binary_sha256':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in (library,exe,b/'libbbgpu.dylib')},
            'expected_modes':args.mode or list(MODES),
            'scope':'Synthetic logical ownership and small offscreen textures; no guest executable, real-game alias coverage or FPS conclusion. Metal currentAllocatedSize is an allocation ledger, not OS physical decommit proof.'}
    if args.build_only:
        report.update(build_only=True,status='built');(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');return
    env={k:v for k,v in os.environ.items() if not k.startswith(('BB_','MTL_'))}
    for key,d in [('BB_USER_DIR','user'),('BB_GPU_USER_DIR','gpu'),('BB_DATA_DIR','data'),('BB_CONFIG_DIR','config')]:env[key]=str(out/d)
    env.update(BB_TEST_MAPPING_LIBRARY=str(library),MTL_DEBUG_LAYER='1',MTL_DEBUG_LAYER_ERROR_MODE='assert',
               BB_MTL_CONTINUE_CLEARS='1',BB_MTL_QUERY_WRITE_EPOCH='1',BB_MTL_INLINE_UPDATES='1',BB_MTL_RENDER_UPDATES='1',
               BB_MTL_BATCH_CLEARS='1',BB_MTL_NATIVE_CLEAR_BATCH='1',BB_MTL_FOLD_CLEARS='1',BB_LAYER_USE_CACHE='1',BB_MTL_COPY_PAIR='1')
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    try:
        for mode in report['expected_modes']:
            log_path=out/(mode+'.log')
            with log_path.open('w') as log:
                done=subprocess.run([str(exe),mode],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=45)
            if done.returncode:raise RuntimeError(mode+' failed: '+str(done.returncode)+'; see log')
            if log_path.stat().st_size>2*1024*1024:raise RuntimeError('Unexpected oversized test log')
            rows=[json.loads(line[9:]) for line in log_path.read_text().splitlines() if line.startswith('JOINT100 ')]
            assert len(rows)==1;report['runs'].append(dict(mode=mode,**rows[0]));print(mode,rows[0],flush=True)
            (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
        report['status']='complete'
    except BaseException as error:
        report.update(status='failed',error=str(error));raise
    finally:
        (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
