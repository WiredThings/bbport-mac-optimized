#!/usr/bin/env python3
"""Install the optional network dylib into an owned staging app; never an installed app."""
from pathlib import Path
import json
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
from package_optimized import ROOT, SYSTEM, commands, copy, loads, minimum, run, sha


def export_online_sources(destination, cache):
    """Export pristine pinned upstream trees, never a dependency build directory."""
    destination, cache = Path(destination), Path(cache)
    cache.mkdir(parents=True, exist_ok=True)
    pins = json.loads((ROOT/'gpu/bbnet/DEPENDENCIES.json').read_text())
    records = []
    for name, pin in pins.items():
        archive = cache/(name + '-' + pin['commit'] + '.tar.gz')
        if not archive.is_file():
            url = 'https://codeload.github.com/' + pin['repository'] + '/tar.gz/' + pin['commit']
            request = urllib.request.Request(url, headers={'User-Agent':'Blood-Echoes-source-export'})
            with urllib.request.urlopen(request, timeout=120) as response:
                payload = response.read()
            stage = archive.with_suffix('.download')
            stage.write_bytes(payload); stage.replace(archive)
        with tempfile.TemporaryDirectory(prefix='bb-online-source-') as directory:
            with tarfile.open(archive) as source:
                source.extractall(directory, filter='data')
            roots = list(Path(directory).iterdir())
            if len(roots) != 1 or not roots[0].is_dir():
                raise ValueError('Unexpected dependency source archive: ' + name)
            target = destination/name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(roots[0], target)
        records.append(dict(component='online/'+name, repository=pin['repository'],
                            revision=pin['commit'], version=pin['version'], archive_sha256=sha(archive)))
    return records


def install_online_module(app, library, dependencies):
    app, library, dependencies = Path(app), Path(library), Path(dependencies)
    if app.resolve().parent == Path('/Applications'):
        raise ValueError('Use a fresh staging app')
    if run('lipo', '-archs', library) != 'x86_64' or minimum(library) > (26, 0):
        raise ValueError('The network module must support x86_64 macOS 26')
    engine = app/'Contents/Resources/bbport'
    renderer = engine/'bin/gpu-metal/libbbgpu.dylib'
    before = sha(renderer)
    target = engine/'bin/gpu-net/libbbnet.dylib'
    copy(library, target)
    for reference in loads(target):
        if reference.startswith(SYSTEM): continue
        relative = engine/'deps-x86_64/lib'/Path(reference).name
        if not relative.is_file(): raise ValueError('Unpackaged dependency: ' + reference)
        import os
        subprocess.run(['install_name_tool', '-change', reference,
                        '@loader_path/' + os.path.relpath(relative, target.parent), str(target)], check=True)
    for command, value in commands(target):
        if command == 'LC_RPATH':
            subprocess.run(['install_name_tool', '-delete_rpath', value, str(target)], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(target)], check=True)
    copy(ROOT/'scripts/online.py', engine/'scripts/online.py')
    for name in ['COPYING.bbhost', 'SHADNET-PORT.md', 'UPSTREAM.json', 'MAC-CHANGES.patch']:
        copy(ROOT/'gpu/bbnet'/name, engine/'licenses/online'/name)
    copy(ROOT/'gpu/bbnet/third_party/httplib/LICENSE', engine/'licenses/online/cpp-httplib-LICENSE')
    for source, name in [('openssl/LICENSE.txt', 'OpenSSL-LICENSE'), ('protobuf/LICENSE', 'Protobuf-LICENSE'),
                         ('miniupnp/miniupnpc/LICENSE', 'MiniUPnP-LICENSE'), ('json/LICENSE.MIT', 'nlohmann-json-LICENSE')]:
        copy(dependencies/'src'/source, engine/'licenses/online'/name)
    notice = engine/'THIRD-PARTY-NOTICES.txt'
    with notice.open('a', encoding='utf-8') as output:
        output.write('\nOptional online module: Supermedo bbnet / shadPS4 / bbhost THD; '
                     'bbhost-derived code is GPL-3.0 (see licenses/online/COPYING.bbhost). '
                     'Also uses OpenSSL, Protobuf, MiniUPnP, nlohmann/json and cpp-httplib. '
                     'Upstream revision and Mac adaptation diff: licenses/online/.\n')
    assert sha(renderer) == before, 'Online packaging changed the renderer'
    return dict(library_sha256=sha(target), offline_dynamic_load=False,
                renderer_unchanged=True, backend='The Hunters Dream', abi=2)
