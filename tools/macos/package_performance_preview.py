#!/usr/bin/env python3
"""Overlay a rebuilt native renderer on a verified 0.1.2 app, without rebuilding its CPU runtime."""
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
    args = parser.parse_args()
    build_cache = (args.gpu_build / 'CMakeCache.txt').read_text()
    assert 'BB_LTO:BOOL=ON' in build_cache, 'Build the preview with BB_LTO=ON'
    assert '-mcpu=apple-m4' in build_cache, 'This preview manifest requires the M4 build'
    baseline, app = args.baseline.resolve(), args.app.resolve()
    assert not app.exists(), 'Use a new staging path; existing apps are never overwritten'
    old_info = plistlib.loads((baseline / 'Contents/Info.plist').read_bytes())
    assert old_info['CFBundleIdentifier'] == 'io.github.bbport.optimized'
    assert old_info['CFBundleShortVersionString'] == '0.1.2'
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
    for relative in ('tools/macos/run_optimized.sh', 'scripts/patches.py'):
        copy(ROOT / relative, engine / relative)
    (engine / '.version').write_text('0.1.3-perf1\n')
    copy(ROOT / 'docs/release/PERFORMANCE-PORT-1.md', engine / 'PERFORMANCE-PORT-1.md')
    info = dict(old_info, CFBundleShortVersionString='0.1.3', CFBundleVersion='103')
    (app / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
    manifest = {
        'version': '0.1.3-perf1', 'source_commit': run('git', '-C', ROOT, 'rev-parse', 'HEAD'),
        'cpu_target': 'apple-m4', 'lto': True,
        'baseline_version': '0.1.2-zh', 'renderer_inputs': changed,
        'game_started_for_validation': False, 'gameplay_performance_verified': False,
        'upstream_commits': ['5137a3ec646e0320205f868ea86972b2f45f185d',
                             'fe06953cebeba0cf116f7d73842623a902db5abe',
                             '29bd6c822a24c4c0cd55ffce0dbf3f8d58d317ea'],
    }
    (engine / 'PERFORMANCE-PORT.json').write_text(json.dumps(manifest, indent=2) + '\n')
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
    archive = args.output / 'Bloodborne-Optimized-0.1.3-perf1.zip'
    subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(app), str(archive)], check=True)
    manifest['archive_sha256'] = sha(archive)
    (args.output / 'performance-port-1-verification.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(archive)


if __name__ == '__main__':
    main()
