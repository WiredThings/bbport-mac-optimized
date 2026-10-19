#!/usr/bin/env python3
"""Build an allowlisted, relocatable local preview and its corresponding source archive."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SYSTEM = ('/usr/lib/', '/System/')
SCRIPTS = ('ultrawide_ui.py', 'mods.py', 'chinese_translation.py', 'prepare.py', 'game_check.py', 'link_libc.py', 'link_modules.py', 'content_profile.py', 'patches.py')
FORBIDDEN = {'eboot.bin', 'param.sfo', 'icon0.png', 'pic0.png', 'pic1.png', 'sce_module', 'dvdroot_ps4',
             'savedata', 'quicksaves', 'ui-mod-backups', 'mesa-cache', 'shader-cache', 'last-run.log', 'bbport.ini', '.DS_Store', '.git'}

def run(*args, **kw):
    return subprocess.check_output([str(x) for x in args], text=True, **kw).strip()

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''): h.update(block)
    return h.hexdigest()

def copy(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)
    shutil.copymode(src, dst)
    dst.chmod(dst.stat().st_mode | 0o200)

def commands(path):
    result = []
    for block in re.split(r'Load command \d+\n', run('/usr/bin/otool', '-l', path)):
        cmd = re.search(r'^\s*cmd (LC_\w+)', block, re.M)
        name = re.search(r'^\s*(?:name|path) (.+) \(offset \d+\)', block, re.M)
        if cmd and name: result.append((cmd[1], name[1]))
    return result

def loads(path):
    return [v for c, v in commands(path) if c in ('LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB', 'LC_LOAD_UPWARD_DYLIB')]

def minimum(path):
    text = run('/usr/bin/otool', '-l', path)
    matches = re.findall(r'^\s*minos ([\d.]+)', text, re.M)
    if not matches: matches = re.findall(r'LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version ([\d.]+)', text)
    return max((tuple(map(int, v.split('.'))) for v in matches), default=(0,))

# Local automation has a separate development distribution, never a public app payload.
DEVELOPMENT_TOOL_NAMES = {
    'benchmark.py', 'benchmark_candidates.py', 'benchmark_control.py',
    'benchmark_control_client.py', 'benchmark_routes.py', 'build_dev_world_warper.py',
}

def development_tool(relative):
    path = Path(relative)
    return (len(path.parts) >= 3 and path.parts[:2] == ('tools', 'macos') and
            (path.parts[2] == 'dev' or path.name in DEVELOPMENT_TOOL_NAMES or
             path.name.startswith('analyze_metal_') or path.name == 'analyze_texture_retention.py'))

def strip_development_tools(engine):
    """Remove only known developer tool paths from an owned staging app, never user data."""
    engine = Path(engine)
    removed = []
    for path in sorted(engine.rglob('*'), reverse=True):
        relative = path.relative_to(engine)
        if development_tool(relative):
            if path.is_file() or path.is_symlink():
                path.unlink(); removed.append(str(relative))
            elif path.is_dir():
                path.rmdir()
    return sorted(removed)


def inspect_payload(root, *, source=False):
    files = []
    for p in sorted(root.rglob('*')):
        rel = p.relative_to(root)
        if not source:
            assert not any(development_tool(Path(*rel.parts[i:])) for i in range(len(rel.parts))), f'Development tool in public app: {rel}'
        assert not (set(rel.parts) & FORBIDDEN), f'Prohibited file: {rel}'
        assert not re.search(r'(SPRJ\d+|\.sl2$|\.pkg$|\.self$|\.sprx$|\.gfx$|\.dcx$)', str(rel), re.I), f'Game/save file: {rel}'
        if p.is_symlink():
            assert source and not os.path.isabs(os.readlink(p)) and p.resolve().is_relative_to(root.resolve()), f'Unsafe symlink: {rel}'
            files.append({'path': str(rel), 'symlink': os.readlink(p)})
            continue
        if p.is_file():
            if not source:
                assert p.name != 'DEVELOPMENT-WORLD-WARPER.json', 'Development app cannot be released'
                if p.name in ('bb-probe', 'bb-probe-metal'):
                    assert b'BB_DEVELOPMENT_WORLD_WARPER_V1' not in p.read_bytes(), 'Development world warper in release probe'
            files.append({'path': str(rel), 'bytes': p.stat().st_size, 'sha256': sha(p)})
    return files


def export_git(src, dst, records, label):
    """Only indexed source files, recursively including submodules and local source edits."""
    revision = run('git', '-C', src, 'rev-parse', 'HEAD')
    records.append({'component': label, 'revision': revision})
    entries = subprocess.check_output(['git', '-C', str(src), 'ls-files', '--stage', '-z']).split(b'\0')
    for entry in entries:
        if not entry: continue
        meta, name = entry.split(b'\t', 1)
        mode = meta.split()[0]
        rel = Path(os.fsdecode(name))
        source, dest = src / rel, dst / rel
        if mode == b'160000':
            assert (source / '.git').exists(), f'Initialize submodule {source}'
            export_git(source, dest, records, label + '/' + str(rel))
        elif mode == b'120000':
            # Preserve source links without following them into build or user directories.
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.symlink_to(os.readlink(source))
        elif source.is_file():
            copy(source, dest)
        else: raise RuntimeError(f'Missing tracked source: {source}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('version')
    parser.add_argument('--native', type=Path, default=Path(os.environ.get('BB_PACKAGE_NATIVE', ROOT)))
    parser.add_argument('--python-prefix', type=Path, default=Path(os.environ.get('BB_PACKAGE_PYTHON', '/opt/homebrew/opt/python@3.11/Frameworks/Python.framework/Versions/3.11')))
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    parser.add_argument('--skip-launcher-build', action='store_true', help='Reuse the launcher already staged by an earlier invocation')
    args = parser.parse_args()
    assert re.fullmatch(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9.]+)?', args.version), 'Use a semantic version without a leading v'
    native, py = args.native.resolve(), args.python_prefix.resolve()
    assert (py / 'bin/python3.11').is_file(), 'Set BB_PACKAGE_PYTHON to a Python 3.11 framework prefix'
    # Reusing tested binaries is allowed only when their checked-out runtime source matches.
    compared = 0
    for name in run('git', '-C', native, 'ls-files', 'src', 'gpu').splitlines():
        source = native / name
        if source.is_file() and not source.is_symlink():
            assert (ROOT / name).is_file() and sha(source) == sha(ROOT / name), f'Runtime source mismatch: {name}'
            compared += 1
    for name in ('src/runtime_quicksave.c', 'src/runtime_quicksave.h', 'src/runtime_quicksave_bindings.h',
                 'gpu/shim/bbport_region_cache.h', 'gpu/shadps4/video_core/cache_storage_worker.h'):
        assert sha(native / name) == sha(ROOT / name), f'Runtime source mismatch: {name}'
    dirty = run('git', '-C', ROOT, 'status', '--porcelain', '--untracked-files=normal', '--ignore-submodules=dirty')
    assert not dirty, 'Commit release source before packaging:\n' + dirty
    name = 'blood-echoes-' + args.version
    stage = ROOT / 'out' / 'package-optimized'
    top = stage / name
    app = top / 'Blood Echoes.app'
    if not args.skip_launcher_build:
        if top.exists(): shutil.rmtree(top)
        top.mkdir(parents=True)
        env = dict(os.environ, BB_APP=str(app), BB_RELEASE=args.version)
        subprocess.run(['bash', str(ROOT / 'tools/macos/build_launcher.sh')], env=env, check=True)
    assert (app / 'Contents/MacOS/bbport').is_file()
    engine = app / 'Contents/Resources/bbport'
    if engine.exists(): shutil.rmtree(engine)
    engine.mkdir(parents=True)
    for name_ in ('run.sh', 'LICENSE', 'tools/macos/run.sh', 'tools/macos/run_optimized.sh', 'tools/macos/quicksave.py', 'patches/Bloodborne.xml'):
        copy(ROOT / name_, engine / name_)
    for name_ in SCRIPTS: copy(ROOT / 'scripts' / name_, engine / 'scripts' / name_)
    copy(ROOT / 'launcher-macos/Sources/BBLauncher/Resources/Artwork/ATTRIBUTION.txt',
         engine / 'licenses/icons/HuntersMark-ATTRIBUTION.txt')
    (engine / '.packaged').touch()
    (engine / '.version').write_text(args.version + '\n')
    inputs, machos, included = [], [], {}

    def relocate(src, dst, arch, libdir, search):
        src = src.resolve()
        key = (arch, src)
        if key in included: return included[key]
        included[key] = dst
        copy(src, dst)
        inputs.append({'path': str(dst.relative_to(engine)), 'sha256_before_relocation': sha(src)})
        assert arch in run('/usr/bin/lipo', '-archs', src).split(), f'Wrong architecture: {src}'
        assert minimum(src) <= (26, 0), f'{src.name} needs macOS {minimum(src)}'
        machos.append(dst)
        for ref in loads(src):
            if ref.startswith(SYSTEM): continue
            base = Path(ref).name
            if base == 'libbbgpu.dylib':
                dep_src = native / ('out/gpu-arm64/libbbgpu.dylib' if arch == 'arm64' else 'out/gpu/libbbgpu.dylib')
                dep_dst = engine / ('bin/gpu-arm64/libbbgpu.dylib' if arch == 'arm64' else 'bin/gpu/libbbgpu.dylib')
            elif base == 'Python': dep_src, dep_dst = py / 'Python', engine / 'python/lib/Python'
            else:
                candidates = [Path(ref)] if ref.startswith('/') else []
                if ref.startswith('@loader_path/'):
                    candidates.insert(0, src.parent / ref[len('@loader_path/'):])
                candidates += [Path(s) / base for s in search]
                dep_src = next((p for p in candidates if p.is_file()), None)
                assert dep_src, f'Cannot resolve {ref} from {src}'
                dep_dst = libdir / base
            target = relocate(dep_src, dep_dst, arch, libdir, search)
            newref = '@loader_path/' + os.path.relpath(target, dst.parent)
            subprocess.run(['install_name_tool', '-change', ref, newref, str(dst)], check=True, stderr=subprocess.DEVNULL)
        for cmd, value in commands(dst):
            if cmd == 'LC_RPATH': subprocess.run(['install_name_tool', '-delete_rpath', value, str(dst)], check=True, stderr=subprocess.DEVNULL)
            elif cmd == 'LC_ID_DYLIB': subprocess.run(['install_name_tool', '-id', '@rpath/' + dst.name, str(dst)], check=True, stderr=subprocess.DEVNULL)
        return dst

    print('Bundling both engine architectures and native driver', flush=True)
    for arch, paths in [('x86_64', ['out/bb-probe', 'out/gpu/libbbgpu.dylib']), ('arm64', ['out/gpu-arm64/bb-gpu', 'out/gpu-arm64/libbbgpu.dylib'])]:
        libdir = engine / ('deps-' + arch) / 'lib'
        for path in paths:
            relocate(native / path, engine / 'bin' / Path(path).relative_to('out'), arch, libdir, [native / ('deps-' + arch) / 'lib'])
    kk = native / 'deps-arm64/lib/kosmickrisp'
    relocate(kk / 'libvulkan_kosmickrisp.dylib', engine / 'deps-arm64/lib/kosmickrisp/libvulkan_kosmickrisp.dylib', 'arm64', engine / 'deps-arm64/lib', [native / 'deps-arm64/lib'])
    manifest = json.loads((kk / 'kosmickrisp_mesa_icd.json').read_text())
    manifest['ICD']['library_path'] = './libvulkan_kosmickrisp.dylib'
    (engine / 'deps-arm64/lib/kosmickrisp/kosmickrisp_mesa_icd.json').write_text(json.dumps(manifest, indent=2) + '\n')

    print('Bundling private Python runtime (system-linked extensions only)', flush=True)
    pyout = engine / 'python'
    relocate(py / 'Resources/Python.app/Contents/MacOS/Python', pyout / 'bin/python3', 'arm64', pyout / 'lib', [py])
    stdlib = py / 'lib/python3.11'
    omitted = []
    for src in sorted(stdlib.rglob('*')):
        rel = src.relative_to(stdlib)
        if set(rel.parts) & {'site-packages', '__pycache__', 'test', 'tests', 'idlelib', 'tkinter', 'turtledemo', 'ensurepip', 'config-3.11-darwin'}: continue
        if src.is_file() and src.suffix == '.py': copy(src, pyout / 'lib/python3.11' / rel)
        elif src.is_file() and src.suffix == '.so':
            # SSL, readline, dbm, bz2, lzma etc. are unnecessary for our internal tools.
            if any(not ref.startswith(SYSTEM) for ref in loads(src)):
                omitted.append(src.name); continue
            relocate(src, pyout / 'lib/python3.11' / rel, 'arm64', pyout / 'lib', [py])
    pyformula = py.parents[3]
    copy(pyformula / 'LICENSE', pyout / 'LICENSE')

    print('Exporting corresponding source and license notices', flush=True)
    source_top = stage / ('blood-echoes-' + args.version + '-source')
    if source_top.exists(): shutil.rmtree(source_top)
    records = []
    export_git(ROOT, source_top / 'project', records, 'blood-echoes')
    seen = set()
    for arch in ('arm64', 'x86_64'):
        for dep in sorted((native / ('deps-' + arch) / 'src').iterdir()):
            if not (dep / '.git').exists(): continue
            # Include each exact revision plus patch state once, even across architectures.
            key = (dep.name, run('git', '-C', dep, 'rev-parse', 'HEAD'), run('git', '-C', dep, 'diff', '--binary'))
            if key in seen: continue
            seen.add(key)
            export_git(dep, source_top / 'dependencies' / arch / dep.name, records, arch + '/' + dep.name)
    # Header-only libraries linked into the engine also retain their notices and source headers.
    if (ROOT/'gpu/bbnet/DEPENDENCIES.json').is_file():
        from package_online import export_online_sources
        online_prefix = Path(os.environ.get('BB_ONLINE_DEPS', ROOT/'deps-online-x86_64'))
        records.extend(export_online_sources(source_top/'dependencies/online', online_prefix/'source-archives'))
    for component in ('boost', 'magic_enum', 'robin-map'):
        prefix = Path('/opt/homebrew/opt') / component
        if not prefix.exists(): continue
        dest = source_top / 'dependencies/headers' / component
        if (prefix / 'include').exists(): shutil.copytree(prefix / 'include', dest / 'include', symlinks=False)
        for item in prefix.iterdir():
            if item.is_file() and re.match(r'(LICENSE|COPYING|NOTICE)', item.name, re.I): copy(item, dest / item.name)
    (source_top / 'SOURCE-MANIFEST.json').write_text(json.dumps(records, indent=2) + '\n')
    copy(ROOT / 'docs/release/BUILD-OPTIMIZED.md', source_top / 'BUILD.md')
    licenses = engine / 'licenses'
    for src in sorted(source_top.rglob('*')):
        if src.is_file() and re.match(r'(LICENSE|COPYING|COPYRIGHT|NOTICE)([._-]|$)', src.name, re.I):
            copy(src, licenses / src.relative_to(source_top))
    copy(pyformula / 'LICENSE', licenses / 'Python-3.11-LICENSE.txt')
    if (pyformula / '.brew/python@3.11.rb').exists(): copy(pyformula / '.brew/python@3.11.rb', source_top / 'dependencies/Python-homebrew-formula.rb')
    python_version = run(py / 'bin/python3.11', '--version')
    source_revision = run('git', '-C', ROOT, 'rev-parse', 'HEAD')
    notices = f'''Open-source components for Blood Echoes {args.version}\n\nThe port is GPL-2.0-or-later; see LICENSE. Application source, build scripts,
pinned dependency sources and local patches are available at:
https://github.com/WiredThings/blood-echoes/tree/{source_revision}
Source retrieval and rebuild instructions:
https://github.com/WiredThings/blood-echoes/blob/{source_revision}/docs/release/SOURCE.md
Component license texts are in licenses/.\n\nComponents: bbport, shadPS4, KosmicKrisp/Mesa, Vulkan Loader/Headers, SDL3, FFmpeg\n(LGPL build, no GPL/nonfree codecs), fmt, xxHash, miniz, Zydis/Zycore, LibAtrac9,\nImGui, FSR-Vulkan/FidelityFX, sirit/SPIRV-Headers, VMA, xbyak, Boost, magic_enum,\nrobin-map and DejaVu fonts. See SOURCE-MANIFEST.json for revisions.\n\nPrivate preparation runtime: {python_version}, PSF license, built by Homebrew.\nIts standard library is included; optional extensions using external libraries\nare omitted. CPython source: https://www.python.org/downloads/source/\nNo Python third-party packages are included.\n\nNo game executables, large game data, UI packs or user saves are included. Launcher icon artwork is credited separately.\n'''
    (engine / 'THIRD-PARTY-NOTICES.txt').write_text(notices)
    (source_top / 'THIRD-PARTY-NOTICES.txt').write_text(notices)
    copy(source_top / 'SOURCE-MANIFEST.json', engine / 'SOURCE-MANIFEST.json')

    print('Checking library resolution, versions and signatures', flush=True)
    for file in machos:
        for ref in loads(file):
            if ref.startswith(SYSTEM): continue
            assert ref.startswith('@loader_path/'), f'External load {file}: {ref}'
            target = (file.parent / ref[len('@loader_path/'):]).resolve()
            assert target.is_relative_to(engine.resolve()) and target.is_file(), f'Missing load {file}: {ref}'
        assert all(not value.startswith('/') for cmd, value in commands(file) if cmd == 'LC_RPATH')
        subprocess.run(['codesign', '--force', '--sign', '-', str(file)], check=True, stderr=subprocess.DEVNULL)
    info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    assert 'BBRepoPath' not in info and info.get('CFBundleIconFile') == 'AppIcon'
    subprocess.run(['codesign', '--force', '--sign', '-', str(app)], check=True, stderr=subprocess.DEVNULL)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    with tempfile.TemporaryDirectory(prefix='bbport-package-smoke-') as t:
        env = {k: v for k, v in os.environ.items() if not k.startswith(('BB_', 'MTLCAPTURE_'))
               and k not in ('MTL_CAPTURE_ENABLED', 'DYLD_INSERT_LIBRARIES', 'DYLD_LIBRARY_PATH')}
        env.update(PATH='/usr/bin:/bin:/usr/sbin:/sbin', PYTHONHOME=str(pyout),
                   PYTHONNOUSERSITE='1', PYTHONDONTWRITEBYTECODE='1', TMPDIR=t,
                   BB_GAME_DIR=t+'/no-game', BB_USER_DIR=t+'/user', BB_DATA_DIR=t+'/data',
                   BB_GPU_USER_DIR=t+'/cache', BB_CONFIG=t+'/missing.ini')
        subprocess.run([str(pyout / 'bin/python3'), '-c', 'import json,hashlib,fcntl,shutil,tempfile,subprocess,uuid,xml.etree.ElementTree; assert hashlib.sha256(b"abc").hexdigest().startswith("ba7816"); print("Bundled Python imports OK")'], env=env, check=True)
        for script in ('scripts/patches.py', 'scripts/prepare.py', 'scripts/ultrawide_ui.py', 'tools/macos/quicksave.py'):
            subprocess.run([str(pyout / 'bin/python3'), str(engine / script), '--help'], env=env, check=True, stdout=subprocess.DEVNULL)
        probe = subprocess.run([str(engine / 'bin/bb-probe')], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        assert 'Usage:' in probe.stdout, f'bb-probe exit {probe.returncode}: {probe.stdout}'
        print('Rosetta runtime starts with no Homebrew environment', flush=True)
    # Verify smoke checks did not write bytecode into the signed app.
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    copy(ROOT / 'docs/release/READ-ME-FIRST.txt', top / 'Read Me First.txt')
    copy(ROOT / 'docs/release/0.1.0.md', top / 'Release Notes.md')
    copy(ROOT / 'LICENSE', top / 'LICENSE.txt')
    strip_development_tools(engine)
    # Contents changed after smoke checks: refresh only the outer app signature.
    subprocess.run(['codesign', '--force', '--sign', '-', str(app)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    files = inspect_payload(top)
    source_files = inspect_payload(source_top, source=True)
    audit = {'version': args.version, 'commit': run('git', '-C', ROOT, 'rev-parse', 'HEAD'),
             'runtime_source_files_compared': compared, 'macho_count': len(machos),
             'python': python_version, 'omitted_python_extensions': omitted,
             'signature': 'ad hoc; not notarized', 'game_files_and_saves': 'excluded by allowlist and path audit',
             'inputs': inputs, 'files': files, 'source_file_count': len(source_files)}
    (top / 'Package Audit.json').write_text(json.dumps(audit, indent=2) + '\n')
    args.output.mkdir(parents=True, exist_ok=True)
    zip_path = args.output / (name + '.zip')
    zip_path.unlink(missing_ok=True)
    subprocess.run(['/usr/bin/ditto', '-c', '-k', '--norsrc', '--keepParent', str(top), str(zip_path)], check=True)
    source_path = args.output / (source_top.name + '.tar.gz')
    with tarfile.open(source_path, 'w:gz') as tar: tar.add(source_top, arcname=source_top.name)
    checksum_path = args.output / (name + '-SHA256SUMS.txt')
    checksum_path.write_text(''.join(f'{sha(p)}  {p.name}\n' for p in (zip_path, source_path)))
    (args.output / (name + '-audit.json')).write_text(json.dumps(audit, indent=2) + '\n')
    print(f'Built {zip_path}\nMatching source: {source_path}\nChecksums: {checksum_path}', flush=True)

if __name__ == '__main__': main()
