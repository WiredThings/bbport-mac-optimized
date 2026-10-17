#!/usr/bin/env python3
"""Overlay the Metal backend and localized launcher on a verified 0.1.4-mem1 app."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess

from package_optimized import ROOT, SYSTEM, commands, copy, inspect_payload, loads, minimum, run, sha

VERSION = '0.2.0-metal1'
UPSTREAM = '11992198df6306dacf6ec37de57ee920db1acc11'


def text_hash(path):
    # Changing load commands/signatures must not change any runtime instructions.
    lines = run('otool', '-s', '__TEXT', '__text', path).splitlines()[2:]
    code = ''.join(''.join(line.split()[1:]) for line in lines)
    return hashlib.sha256(bytes.fromhex(code)).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--gpu-build', type=Path, required=True)
    parser.add_argument('--metal-probe', type=Path, help='Optional rebuilt Metal-only probe with memory snapshot ABI')
    parser.add_argument('--launcher', type=Path, required=True)
    parser.add_argument('--spirv-cross-source', type=Path, required=True)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', default=VERSION)
    parser.add_argument('--verification-name', default='metal-backend')
    args = parser.parse_args()
    assert re.fullmatch(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?', args.version)
    assert re.fullmatch(r'[A-Za-z0-9-]+', args.verification_name)
    baseline, app = args.baseline.resolve(), args.app.resolve()
    assert not app.exists(), 'Use a fresh staging app; never overwrite an installed app here'
    old_info = plistlib.loads((baseline / 'Contents/Info.plist').read_bytes())
    assert old_info['CFBundleIdentifier'] == 'io.github.bbport.optimized'
    assert old_info['CFBundleShortVersionString'] in ['0.1.4', '0.2.0', '0.2.1', '0.2.2', '0.2.3', '0.2.4', '0.2.5', '0.2.6', '0.2.7', '0.2.8', '0.2.9', '0.2.10', '0.2.11', '0.2.12', '0.2.13', '0.2.14'], 'Expected verified mem1 or Metal baseline'
    assert run('git', '-C', ROOT / 'engines/metal', 'rev-parse', 'HEAD') == UPSTREAM
    cache = (args.gpu_build / 'CMakeCache.txt').read_text()
    assert re.search(r'CMAKE_OSX_ARCHITECTURES:[^=]+=x86_64', cache)
    assert 'BB_LTO:BOOL=ON' in cache
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(baseline)], check=True)
    app.parent.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    subprocess.run(['ditto', str(baseline), str(app)], check=True)
    engine = app / 'Contents/Resources/bbport'
    native = engine / 'bin/gpu-metal'
    lib = native / 'libbbgpu.dylib'
    source = args.gpu_build / 'libbbgpu.dylib'
    assert run('lipo', '-archs', source) == 'x86_64'
    assert minimum(source) <= (26, 0)
    copy(source, lib)
    for ref in loads(source):
        if ref.startswith(SYSTEM):
            continue
        target = engine / 'deps-x86_64/lib' / Path(ref).name
        assert target.is_file(), f'Unexpected new dynamic dependency: {ref}'
        subprocess.run(['install_name_tool', '-change', ref,
                        '@loader_path/' + os.path.relpath(target, lib.parent), str(lib)], check=True)
    for cmd, value in commands(lib):
        if cmd == 'LC_RPATH':
            subprocess.run(['install_name_tool', '-delete_rpath', value, str(lib)], check=True)
    probe = engine / 'bin/bb-probe-metal'
    original = engine / 'bin/bb-probe'
    copy(args.metal_probe or original, probe)
    old_gpu = next(x for x in loads(probe) if x.endswith('/libbbgpu.dylib'))
    subprocess.run(['install_name_tool', '-change', old_gpu,
                    '@loader_path/gpu-metal/libbbgpu.dylib', str(probe)], check=True)
    if args.metal_probe:
        assert run('lipo', '-archs', probe) == 'x86_64'
        assert '_runtime_memory_snapshot_v1' in run('nm', '-g', probe), 'Missing memory snapshot ABI'
        for ref in loads(probe):
            if ref.startswith(SYSTEM) or ref.startswith('@loader_path/'): continue
            target = engine / 'deps-x86_64/lib' / Path(ref).name
            assert target.is_file(), f'Unexpected probe dependency: {ref}'
            subprocess.run(['install_name_tool', '-change', ref, '@loader_path/' + os.path.relpath(target, probe.parent), str(probe)], check=True)
        for cmd, value in commands(probe):
            if cmd == 'LC_RPATH': subprocess.run(['install_name_tool', '-delete_rpath', value, str(probe)], check=True)
    else:
        assert text_hash(probe) == text_hash(original), 'CPU runtime instructions changed'
    for relative in ['run.sh', 'tools/macos/run_optimized.sh', 'scripts/patches.py']:
        copy(ROOT / relative, engine / relative)
    copy(ROOT / 'docs/release/METAL-BACKEND.md', engine / 'METAL-BACKEND.md')
    copy(ROOT / 'docs/release/METAL-MEMORY-1.md', engine / 'METAL-MEMORY-1.md')
    copy(ROOT / 'docs/release/METAL-PERFORMANCE-1.md', engine / 'METAL-PERFORMANCE-1.md')
    copy(ROOT / 'docs/release/METAL-OCCLUSION-1.md', engine / 'METAL-OCCLUSION-1.md')
    copy(ROOT / 'docs/release/METAL-GAME-MOTION-1.md', engine / 'METAL-GAME-MOTION-1.md')
    copy(ROOT / 'docs/release/METAL-GAME-MOTION-2.md', engine / 'METAL-GAME-MOTION-2.md')
    copy(ROOT / 'docs/release/METAL-MEMORY-2.md', engine / 'METAL-MEMORY-2.md')
    copy(ROOT / 'docs/release/METAL-MEMORY-3.md', engine / 'METAL-MEMORY-3.md')
    copy(ROOT / 'docs/release/METAL-BDA-PAGE-TABLE.md', engine / 'METAL-BDA-PAGE-TABLE.md')
    copy(ROOT / 'docs/release/METAL-GPU-TIMING.md', engine / 'METAL-GPU-TIMING.md')
    copy(ROOT / 'docs/release/METAL-PERFORMANCE-2.md', engine / 'METAL-PERFORMANCE-2.md')
    copy(ROOT / 'docs/release/METAL-PERFORMANCE-3.md', engine / 'METAL-PERFORMANCE-3.md')
    copy(ROOT / 'docs/release/METAL-PERFORMANCE-4.md', engine / 'METAL-PERFORMANCE-4.md')
    copy(ROOT / 'docs/release/METAL-VERTEX-MATH.md', engine / 'METAL-VERTEX-MATH.md')
    copy(ROOT / 'tools/macos/analyze_metal_gpu.py', engine / 'tools/macos/analyze_metal_gpu.py')
    for p in (ROOT / 'engines/metal').glob('*LICENSE*'):
        copy(p, engine / 'licenses/metal' / p.name)
    assert run('git', '-C', args.spirv_cross_source, 'rev-parse', 'HEAD') == 'f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7'
    for name, prefix in [('spirv-cross', args.spirv_cross_source)]:
        for p in prefix.glob('LICENSE*'):
            dest = engine / 'licenses/metal' / name / p.name
            if p.is_dir():
                shutil.copytree(p, dest, dirs_exist_ok=True)
            else:
                copy(p, dest)
    copy(args.launcher / 'Contents/MacOS/bbport', app / 'Contents/MacOS/bbport')
    resource = 'BBLauncher_BBLauncher.bundle'
    subprocess.run(['ditto', str(args.launcher / 'Contents/Resources' / resource),
                    str(app / 'Contents/Resources' / resource)], check=True)
    info = plistlib.loads((args.launcher / 'Contents/Info.plist').read_bytes())
    info['CFBundleShortVersionString'] = args.version.split('-')[0]
    info['CFBundleVersion'] = ''.join(args.version.split('-')[0].split('.'))
    (app / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
    (engine / '.version').write_text(args.version + '\n')
    copy(ROOT / 'docs/release/METAL-PROXY-COMPRESSION.md', engine / 'METAL-PROXY-COMPRESSION.md')
    patches = {p.name: sha(p) for p in sorted((ROOT / 'gpu/patches/metal').glob('*.patch'))}
    manifest = dict(version=args.version, source_commit=run('git', '-C', ROOT, 'rev-parse', 'HEAD'),
                    upstream_repository='https://github.com/Huax-1839/bloodborne-macos',
                    upstream_commit=UPSTREAM, adapter_patches=patches,
                    renderer_architecture='x86_64 under Rosetta, direct Metal API',
                    renderer_input_sha256=sha(source), cpu_text_sha256=text_hash(original), metal_probe_text_sha256=text_hash(probe),
                    metal_probe_snapshot_abi=bool(args.metal_probe),
                    baseline=(baseline / 'Contents/Resources/bbport/.version').read_text().strip(), default_renderer='metal', fallback_renderer='vulkan',
                    bda_table_default='two-level', bda_flat_fallback='BB_MTL_TWO_LEVEL_BDA=0',
                    gpu_stage_timing_default=False, gpu_stage_timing_enable='BB_MTL_GPU_TIMING=1',
                    gpu_stage_timing_counter_bytes_per_slot=32768, gpu_stage_timing_max_slots=4,
                    scene_clear_discard_default=True, scene_clear_discard_fallback='BB_MTL_CLEAR_DISCARD=0',
                    scene_clear_diagnostic_control='BB_MTL_CLEAR_DISCARD_FILE',
                    scene_full_copy_discard_default=True, scene_full_copy_discard_fallback='BB_MTL_OVERWRITE_DISCARD=0',
                    scene_full_copy_diagnostic_control='BB_MTL_OVERWRITE_DISCARD_FILE',
                    scene_proxy_clear_default=True, scene_proxy_clear_fallback='BB_MTL_PROXY_CLEAR=0',
                    scene_proxy_clear_diagnostic_control='BB_MTL_PROXY_CLEAR_FILE',
                    vertex_strict_math_inline_default=True, vertex_strict_math_fallback='BB_MTL_VERTEX_MATH=0',
                    vertex_strict_math_diagnostic_control='BB_MTL_VERTEX_MATH_FILE',
                    vertex_strict_math_reference_library_limit=32, vertex_strict_math_reference_pso_limit=32,
                    scene_proxy_lean_usage_default=True, scene_proxy_lean_usage_fallback='BB_MTL_PROXY_LEAN=0',
                    scene_proxy_format_promotion='on first compatible non-sRGB reinterpretation; old backing/views retired after GPU tick',
                    game_started_for_validation=False, gameplay_performance_verified=False)
    (engine / 'METAL-BACKEND.json').write_text(json.dumps(manifest, indent=2) + '\n')
    notices = engine / 'THIRD-PARTY-NOTICES.txt'
    notices.write_text(notices.read_text() + '\nMetal backend: Huax-1839/bloodborne-macos, revision ' + UPSTREAM +
                       '\nSPIRV-Cross f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7 (Apache-2.0).\n' +
                       'Sampler memo adapted from Awakenchan/BloodBorneButPc 4f93643eb573ae445a24db1d64eabf663a5d5147 (GPL-3.0-or-later).\n' +
                       'Game velocity mirror adapted from IFreemz/shadPS4-Bloodborne-DLSS-FSR 149f63645bbb40fa587daa68fbf003b1f6dbcd7d (GPL-2.0-or-later).\n' +
                       'Integration source and adapter patches: https://github.com/WiredThings/bbport-mac-optimized/tree/optimized\n')
    inspect_payload(engine)
    for p in (lib, probe):
        for ref in loads(p):
            assert ref.startswith(SYSTEM) or (ref.startswith('@loader_path/') and
                   (p.parent / ref.removeprefix('@loader_path/')).resolve().is_file()), (p, ref)
    subprocess.run(['codesign', '--force', '--deep', '--sign', '-', str(app)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    # No arguments exits with usage before guest memory/window/GPU initialization.
    smoke = subprocess.run([str(probe)], env=dict(os.environ, DYLD_BIND_AT_LAUNCH='1'),
                           text=True, capture_output=True, timeout=15)
    assert smoke.returncode == 1 and 'Usage:' in smoke.stderr, smoke.stderr
    manifest['packaged_dyld_smoke_test'] = 'passed, eager binding and usage exit'
    archive = args.output / f'Bloodborne-Optimized-{args.version}.zip'
    subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(app), str(archive)], check=True)
    manifest['archive_sha256'] = sha(archive)
    (args.output / f'{args.verification_name}-verification.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(archive)


if __name__ == '__main__':
    main()
