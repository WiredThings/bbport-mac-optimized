#!/usr/bin/env python3
"""Overlay a rebuilt native renderer and optional memory-fix driver on a verified app."""
import argparse
import json
import os
from pathlib import Path
import plistlib
import subprocess

from package_optimized import ROOT, SYSTEM, commands, copy, inspect_payload, loads, minimum, run, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--gpu-build', type=Path, required=True)
    parser.add_argument('--app', type=Path, required=True, help='new, nonexistent staging app path')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--memory-driver', type=Path,
                        help='directory containing the PR #8 driver and its relative-path ICD')
    args = parser.parse_args()
    memory_fix = args.memory_driver is not None
    version = '0.1.4-mem1' if memory_fix else '0.1.3-perf1'
    verification_name = 'memory-fix-1' if memory_fix else 'performance-port-1'
    build_cache = (args.gpu_build / 'CMakeCache.txt').read_text()
    assert 'BB_LTO:BOOL=ON' in build_cache, 'Build the preview with BB_LTO=ON'
    assert '-mcpu=apple-m4' in build_cache, 'This preview manifest requires the M4 build'
    baseline, app = args.baseline.resolve(), args.app.resolve()
    assert not app.exists(), 'Use a new staging path; existing apps are never overwritten'
    old_info = plistlib.loads((baseline / 'Contents/Info.plist').read_bytes())
    assert old_info['CFBundleIdentifier'] == 'io.github.bbport.optimized'
    assert old_info['CFBundleShortVersionString'] == ('0.1.3' if memory_fix else '0.1.2')
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(baseline)], check=True)
    args.output.mkdir(parents=True, exist_ok=True)
    app.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['ditto', str(baseline), str(app)], check=True)
    engine = app / 'Contents/Resources/bbport'
    native = engine / 'bin/gpu-arm64'
    libdir = engine / 'deps-arm64/lib'
    changed = []

    def relocate(source, dest):
        copy(source, dest)
        assert 'arm64' in run('lipo', '-archs', dest).split()
        assert minimum(dest) <= (26, 0), f'{dest.name} raises the macOS minimum'
        changed.append({'path': str(dest.relative_to(engine)), 'input_sha256': sha(source)})
        for ref in loads(source):
            if ref.startswith(SYSTEM):
                continue
            name = Path(ref).name
            target = native / name if name == 'libbbgpu.dylib' else libdir / name
            # Keep the baseline's exact tested driver and dependency binaries. A new ABI
            # must be rebuilt/packaged explicitly, never silently pulled from Homebrew.
            assert target.is_file(), f'Dependency absent from baseline: {ref}'
            newref = '@loader_path/' + os.path.relpath(target, dest.parent)
            subprocess.run(['install_name_tool', '-change', ref, newref, str(dest)], check=True)
        for cmd, value in commands(dest):
            if cmd == 'LC_RPATH':
                subprocess.run(['install_name_tool', '-delete_rpath', value, str(dest)], check=True)
            elif cmd == 'LC_ID_DYLIB':
                subprocess.run(['install_name_tool', '-id', '@rpath/' + dest.name, str(dest)], check=True)

    relocate(args.gpu_build / 'libbbgpu.dylib', native / 'libbbgpu.dylib')
    relocate(args.gpu_build / 'bb-gpu', native / 'bb-gpu')
    if memory_fix:
        relocate(args.memory_driver / 'libvulkan_kosmickrisp.dylib',
                 libdir / 'kosmickrisp/libvulkan_kosmickrisp.dylib')
        icd = args.memory_driver / 'kosmickrisp_mesa_icd.json'
        assert json.loads(icd.read_text())['ICD']['library_path'] == './libvulkan_kosmickrisp.dylib'
        copy(icd, libdir / 'kosmickrisp/kosmickrisp_mesa_icd.json')
    for relative in ('tools/macos/run_optimized.sh', 'scripts/patches.py'):
        copy(ROOT / relative, engine / relative)
    (engine / '.version').write_text(version + '\n')
    copy(ROOT / 'docs/release/PERFORMANCE-PORT-1.md', engine / 'PERFORMANCE-PORT-1.md')
    if memory_fix:
        copy(ROOT / 'docs/release/MEMORY-FIX-1.md', engine / 'MEMORY-FIX-1.md')
    info = dict(old_info, CFBundleShortVersionString='0.1.4' if memory_fix else '0.1.3',
                CFBundleVersion='104' if memory_fix else '103')
    (app / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
    manifest = {
        'version': version, 'source_commit': run('git', '-C', ROOT, 'rev-parse', 'HEAD'),
        'cpu_target': 'apple-m4', 'lto': True,
        'baseline_version': old_info['CFBundleShortVersionString'], 'renderer_inputs': changed,
        'game_started_for_validation': False, 'gameplay_performance_verified': False,
        'upstream_commits': ['5137a3ec646e0320205f868ea86972b2f45f185d',
                             'fe06953cebeba0cf116f7d73842623a902db5abe',
                             '29bd6c822a24c4c0cd55ffce0dbf3f8d58d317ea'],
    }
    if memory_fix:
        manifest['memory_upstream'] = {
            'pool_trim': 'd6704c65486bf7389ed329a1731f33b6834eea59',
            'graphics_ledger': '4de5fd1340c8184dd88687fc07631ee2425cb587',
            'driver_pr': 'https://github.com/bmy/bbport-mac/pull/8',
            'driver_pr_commit': '827e27c540eece5b0fe2d478b9435d1cb042c72f',
            'driver_wrapper': 'a82cadb14cb225eaeadf190d5c9d20ca8ec91744',
            'mesa': '8b06c1fef1ed316217b805d1068fc74afaa40438',
        }
    (engine / ('MEMORY-FIX.json' if memory_fix else 'PERFORMANCE-PORT.json')).write_text(
        json.dumps(manifest, indent=2) + '\n')
    inspect_payload(engine)
    for item in changed:
        p = engine / item['path']
        for ref in loads(p):
            assert ref.startswith(SYSTEM) or (ref.startswith('@loader_path/') and
                (p.parent / ref.removeprefix('@loader_path/')).resolve().is_file()), ref
    subprocess.run(['codesign', '--force', '--deep', '--sign', '-', str(app)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    # No IPC arguments: this checks dyld/dependency resolution and exits before mapping
    # guest memory, creating a Vulkan device or starting the game.
    probe = subprocess.run([str(native / 'bb-gpu')], capture_output=True, text=True, timeout=15,
                           env=dict(os.environ, DYLD_BIND_AT_LAUNCH='1'))
    assert probe.returncode == 2 and 'the game process starts this' in probe.stderr, probe.stderr
    manifest['packaged_dyld_smoke_test'] = 'passed; usage exit before GPU/guest initialization'
    archive = args.output / f'Blood-Echoes-{version}.zip'
    subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(app), str(archive)], check=True)
    manifest['archive_sha256'] = sha(archive)
    (args.output / f'{verification_name}-verification.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(archive)


if __name__ == '__main__':
    main()
