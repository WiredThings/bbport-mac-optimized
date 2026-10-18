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
import tempfile

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
    parser.add_argument('--benchmark-report', type=Path, help='Completed same-binary ABBA/BAAB suite or eight-run aggregate')
    args = parser.parse_args()
    assert re.fullmatch(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?', args.version)
    assert re.fullmatch(r'[A-Za-z0-9-]+', args.verification_name)
    baseline, app = args.baseline.resolve(), args.app.resolve()
    assert not app.exists(), 'Use a fresh staging app; never overwrite an installed app here'
    old_info = plistlib.loads((baseline / 'Contents/Info.plist').read_bytes())
    assert old_info['CFBundleIdentifier'] == 'io.github.bbport.optimized'
    assert old_info['CFBundleShortVersionString'] in ['0.1.4', '0.2.0', '0.2.1', '0.2.2', '0.2.3', '0.2.4', '0.2.5', '0.2.6', '0.2.7', '0.2.8', '0.2.9', '0.2.10', '0.2.11', '0.2.12', '0.2.13', '0.2.14', '0.2.15', '0.2.16', '0.2.17', '0.2.18', '0.2.19', '0.2.20', '0.2.21', '0.2.22', '0.2.23', '0.2.24'], 'Expected verified mem1 or Metal baseline'
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
    gameplay_validation={}
    baseline_manifest_path=baseline/'Contents/Resources/bbport/METAL-BACKEND.json'
    baseline_manifest=json.loads(baseline_manifest_path.read_text()) if baseline_manifest_path.exists() else {}
    prior_gameplay_validation=baseline_manifest.get('gameplay_validation') or baseline_manifest.get('prior_profile_gameplay_validation',{})
    if args.benchmark_report:
        measured=json.loads(args.benchmark_report.read_text())
        assert measured['status']=='complete' and len(measured['runs']) in (4,8)
        labels=[r['label'] for r in measured['runs']]
        if len(labels)==4:
            assert labels==({'ABBA':['a1','b1','b2','a2'],'BAAB':['b1','a1','a2','b2']}[measured.get('order','ABBA')])
        else:
            assert labels==['a1','b1','b2','a2','b1','a1','a2','b2']
        for r in measured['runs']:
            result_path=Path(r['result']);result=json.loads(result_path.read_text())
            assert result['valid'] and result['renderer_sha256']==r['renderer']
            owner=json.loads((result_path.parent/'owner.json').read_text())
            tested=Path(owner['app'])/'Contents/Resources/bbport/bin/gpu-metal/libbbgpu.dylib'
            assert sha(tested)==r['renderer'] and text_hash(tested)==text_hash(source), 'Measured renderer differs'
        gameplay_validation=dict(report=args.benchmark_report.name,guard_policy=measured['guard_policy'],
                                 means=measured['means'],fps_delta_percent=measured.get('fps_delta_percent',measured.get('deltas',{}).get('fps')),
                                 selected_flags=measured.get('candidates',[measured['candidate']] if 'candidate' in measured else []),
                                 fixed_flags=measured.get('fixed_variants',{}),order=measured.get('order','ABBA+BAAB'),
                                 runs=len(measured['runs']),isolated_fps_gain_established=False,
                                 scope='Fixed-scene selected-flag comparison; completion is not proof of benefit; >=60 FPS not achieved')
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
    for relative in ['run.sh', 'tools/macos/run_optimized.sh', 'scripts/patches.py', 'tools/macos/benchmark.py', 'tools/macos/benchmark_candidates.py']:
        copy(ROOT / relative, engine / relative)
    copy(ROOT / 'docs/release/METAL-FRAME-CAPTURE.md', engine / 'METAL-FRAME-CAPTURE.md')
    copy(ROOT / 'docs/release/METAL-ATTACHMENT-LIFETIME.md', engine / 'METAL-ATTACHMENT-LIFETIME.md')
    copy(ROOT / 'docs/release/METAL-BENCHMARK-GUARD-V3.md', engine / 'METAL-BENCHMARK-GUARD-V3.md')
    copy(ROOT / 'docs/release/METAL-BACKEND.md', engine / 'METAL-BACKEND.md')
    copy(ROOT / 'docs/release/MACOS-APP-ICONS.md', engine / 'MACOS-APP-ICONS.md')
    copy(ROOT / 'launcher-macos/Sources/BBLauncher/Resources/Artwork/ATTRIBUTION.txt', engine / 'licenses/icons/HuntersMark-ATTRIBUTION.txt')
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
    copy(ROOT / 'tools/macos/analyze_metal_attachments.py', engine / 'tools/macos/analyze_metal_attachments.py')
    copy(ROOT / 'tools/macos/analyze_metal_transfers.py', engine / 'tools/macos/analyze_metal_transfers.py')
    copy(ROOT / 'docs/release/METAL-OPTIMIZATION-CONTROLS.md', engine / 'METAL-OPTIMIZATION-CONTROLS.md')
    copy(ROOT / 'docs/release/METAL-SMALL-TRANSFERS.md', engine / 'METAL-SMALL-TRANSFERS.md')
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
    copy(args.launcher / 'Contents/Resources/AppIcon.icns', app / 'Contents/Resources/AppIcon.icns')
    resource = 'BBLauncher_BBLauncher.bundle'
    subprocess.run(['ditto', str(args.launcher / 'Contents/Resources' / resource),
                    str(app / 'Contents/Resources' / resource)], check=True)
    info = plistlib.loads((args.launcher / 'Contents/Info.plist').read_bytes())
    assert info.get('CFBundleIconFile')=='AppIcon', 'Launcher must declare its bundled app icon'
    info['CFBundleShortVersionString'] = args.version.split('-')[0]
    info['CFBundleVersion'] = ''.join(args.version.split('-')[0].split('.'))
    (app / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
    (engine / '.version').write_text(args.version + '\n')
    copy(ROOT / 'docs/release/METAL-PROXY-COMPRESSION.md', engine / 'METAL-PROXY-COMPRESSION.md')
    copy(ROOT / 'docs/release/METAL-ALPHA-MERGE.md', engine / 'METAL-ALPHA-MERGE.md')
    copy(ROOT / 'docs/release/METAL-SAMPLING-BENCHMARK.md', engine / 'METAL-SAMPLING-BENCHMARK.md')
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
                    upscale_alpha_fusion_default=True, upscale_alpha_fallback='BB_MTL_UPSCALE_ALPHA=0',
                    upscale_alpha_diagnostic_control='BB_MTL_UPSCALE_ALPHA_FILE',
                    metal_quality_presets=[1.0, 1.5, 1.7, 2.0, 3.0, 1.25],
                    fragment_uniform_specialization_default=False, fragment_uniform_specialization_enable='BB_MTL_FRAGMENT_UNIFORMS=1',
                    color_clear_batching_default=True,color_clear_batching_enable='BB_MTL_BATCH_CLEARS=1',
                    pending_color_clear_folding_default=True,pending_color_clear_folding_fallback='BB_MTL_FOLD_CLEARS=0',
                    pending_color_clear_folding_decision='Owner authorized default ON after inconclusive isolated ABBA/BAAB; no isolated FPS gain claimed',
                    ordered_inline_buffer_updates_default=False,
                    ordered_inline_buffer_updates_enable='BB_MTL_INLINE_UPDATES=1',
                    ordered_inline_buffer_updates_max_bytes=256,
                    prewarmed_pipeline_archive_saved_before_gameplay=False,
                    prewarmed_pipeline_archive_persist_enable='BB_MTL_PERSIST_WARM_ARCHIVE=1',
                    prewarmed_pipeline_archive_object_released=True,
                    prewarmed_pipeline_archive_keep_fallback='BB_MTL_RELEASE_WARM_ARCHIVE=0',
                    pipeline_archive_disk_reads_and_automatic_writes_opt_in=True,
                    launcher_optimization_controls=23,
                    launcher_optimization_default_on=19,
                    launcher_optimization_default_off=4,
                    offline_library_experiment_removed=True,
                    compile_audit_default=False,compile_audit_enable='BB_MTL_COMPILE_AUDIT=1 (private diagnostic benchmark only)',
                    combined_metal_profile_default={key:'1' for key in (
                        'BB_MTL_BATCH_CLEARS','BB_MTL_DIRECT_COPY','BB_MTL_ADAPTIVE_STAGING',
                        'BB_MTL_TEXTURE_TILING','BB_MTL_PRECISE_RESIDENCY','BB_MTL_COALESCE_BARRIERS',
                        'BB_MTL_QUERY_COMPUTE_CHAIN','BB_MTL_PRECISE_COMPUTE','BB_MTL_TEXTURE_TILING_HALF','BB_MTL_FOLD_CLEARS')},
                    combined_profile_source='Metal launch entry defaults; explicit zero overrides retained',
                    combined_profile_memory_tradeoff='Owner accepted prior ~105 MiB profile increase and current default ON despite noisy low-frame results; no zero-memory-regression claim',
                    benchmark_guard_policy='critical-sustained-swap-session-v3',
                    attachment_metadata_capture='--capture-native',attachment_metadata_max_bytes=2097152,
                    benchmark_entry='tools/macos/benchmark.py', benchmark_input_isolated=True,
                    scene_proxy_lean_usage_default=True, scene_proxy_lean_usage_fallback='BB_MTL_PROXY_LEAN=0',
                    scene_proxy_format_promotion='on first compatible non-sRGB reinterpretation; old backing/views retired after GPU tick',
                    game_started_for_validation=bool(gameplay_validation),gameplay_comparison_completed=bool(gameplay_validation),
                    gameplay_performance_verified=False, # Completed comparison is not a proven gain.
                    gameplay_validation=gameplay_validation,
                    prior_profile_gameplay_validation=prior_gameplay_validation,
                    app_icon='Hunter\'s Mark game UI artwork, rounded/padded, fixed across launcher startup',
                    game_dock_icon='Original user game sce_sys/icon0.png, rounded/padded once by Cocoa WindowSDL in game process',
                    update_scope='Release startup compilation archive metadata while retaining live PSOs; binary persistence and inline writes stay opt-in after budget/performance checks; quality and audio unchanged')
    (engine / 'METAL-BACKEND.json').write_text(json.dumps(manifest, indent=2) + '\n')
    notices = engine / 'THIRD-PARTY-NOTICES.txt'
    notices.write_text(notices.read_text().replace('No Sony/FromSoftware game files, game artwork, or user saves are included.', 'No game executables, large game data, UI packs or user saves are included. Launcher icon artwork is credited separately.') + '\nMetal backend: Huax-1839/bloodborne-macos, revision ' + UPSTREAM +
                       '\nSPIRV-Cross f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7 (Apache-2.0).\n' +
                       'Sampler memo adapted from Awakenchan/BloodBorneButPc 4f93643eb573ae445a24db1d64eabf663a5d5147 (GPL-3.0-or-later).\n' +
                       'Game velocity mirror adapted from IFreemz/shadPS4-Bloodborne-DLSS-FSR 149f63645bbb40fa587daa68fbf003b1f6dbcd7d (GPL-2.0-or-later).\n' +
                       'Launcher icon: Bloodborne Hunter\'s Mark in-game artwork, hosted by Bloodborne Wiki; see licenses/icons/HuntersMark-ATTRIBUTION.txt.\n' +
                       'Integration source and adapter patches: https://github.com/WiredThings/bbport-mac-optimized/tree/optimized\n')
    inspect_payload(engine)
    for p in (lib, probe):
        for ref in loads(p):
            assert ref.startswith(SYSTEM) or (ref.startswith('@loader_path/') and
                   (p.parent / ref.removeprefix('@loader_path/')).resolve().is_file()), (p, ref)
    subprocess.run(['codesign', '--force', '--deep', '--sign', '-', str(app)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    # No arguments exits with usage before guest memory/window/GPU initialization.
    with tempfile.TemporaryDirectory(prefix='bbport-isolated-link-check-') as directory:
        # Even the usage-only link check gets no access to normal game/user configuration.
        env={k:v for k,v in os.environ.items() if not k.startswith(('BB_','MTLCAPTURE_')) and k not in ('MTL_CAPTURE_ENABLED','DYLD_INSERT_LIBRARIES','DYLD_LIBRARY_PATH')}
        env.update(DYLD_BIND_AT_LAUNCH='1',BB_GAME_DIR=directory+'/no-game',
                   BB_USER_DIR=directory+'/user',BB_DATA_DIR=directory+'/data',
                   BB_GPU_USER_DIR=directory+'/cache',BB_CONFIG=directory+'/missing.ini')
        smoke = subprocess.run([str(probe)], env=env,text=True,capture_output=True,timeout=15)
    assert smoke.returncode == 1 and 'Usage:' in smoke.stderr, smoke.stderr
    manifest['packaged_dyld_smoke_test'] = 'passed, eager binding and usage exit'
    archive = args.output / f'Bloodborne-Optimized-{args.version}.zip'
    subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(app), str(archive)], check=True)
    manifest['archive_sha256'] = sha(archive)
    (args.output / f'{args.verification_name}-verification.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(archive)


if __name__ == '__main__':
    main()
