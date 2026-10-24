#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Import user-downloaded GOTY Chinese Retranslation assets without modifying the game."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import sys
import tempfile
import uuid
import zipfile

from mods import mod_files

SOURCE = 'https://www.nexusmods.com/bloodborne/mods/146'
FILES = tuple(f'dvdroot_ps4/{kind}/{locale}/{name}'
              for locale in ('engus', 'enggb')
              for kind, names in [('menu', ('font.gfx',)),
                                  ('msg', ('item.msgbnd.dcx', 'menu.msgbnd.dcx'))]
              for name in names)
MAX_FILE = 64 * 1024 * 1024
MAX_TOTAL = 256 * 1024 * 1024
MANIFEST = 'translation.json'


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(65536), b''):
            result.update(block)
    return result.hexdigest()


def safe_parts(name):
    if '\\' in name or '\0' in name or name.startswith('/'):
        raise ValueError('Unsafe archive path')
    parts = PurePosixPath(name).parts
    if not parts or any(p in ('.', '..') or ':' in p for p in name.split('/')):
        raise ValueError('Unsafe archive path')
    return tuple(p.casefold() for p in parts)


def canonical(parts):
    # Match only the six English GOTY assets, allowing an extracted archive wrapper.
    for relative in FILES:
        expected = tuple(relative.split('/'))
        if parts == expected or parts[-3:] == expected[-3:]:
            return relative
    return None


def check_script(names, language):
    locales = {part for parts in names for part in parts if part in ('zhocn', 'zhotw')}
    expected = 'zhocn' if language == '11' else 'zhotw'
    if locales and expected not in locales:
        raise ValueError('The package uses the other Chinese script; download the matching GOTY zh-cn / zh-tw file')


def copy_asset(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    size = 0
    with destination.open('xb') as output:
        while block := source.read(65536):
            size += len(block)
            if size > MAX_FILE:
                raise ValueError('Translation asset exceeds the size limit')
            output.write(block)
    if size == 0:
        raise ValueError('Empty translation asset')


def import_package(source, data, language):
    source, data = Path(source), Path(data)
    data.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='.translation-', dir=data))
    try:
        found, names, total = set(), [], 0

        def add(parts, stream, size):
            nonlocal total
            relative = canonical(parts)
            if relative is None:
                return
            if relative in found:
                raise ValueError('Duplicate translation asset')
            total += size
            if size > MAX_FILE or total > MAX_TOTAL:
                raise ValueError('Translation package exceeds the size limit')
            with stream() as input_file:
                copy_asset(input_file, stage / relative)
            found.add(relative)

        if source.is_symlink():
            raise ValueError('Select a regular ZIP or extracted folder, not a symbolic link')
        if source.is_dir():
            for relative, path in mod_files(source):
                parts = safe_parts(relative.as_posix())
                names.append(parts)
                if len(names) > 1024:
                    raise ValueError('Too many files in translation folder')
                if not stat.S_ISREG(path.stat().st_mode):
                    raise ValueError('Translation assets must be regular files')
                add(parts, lambda p=path: p.open('rb'), path.stat().st_size)
        else:
            with zipfile.ZipFile(source) as archive:
                if len(archive.infolist()) > 1024:
                    raise ValueError('Too many archive entries')
                seen = set()
                for info in archive.infolist():
                    parts = safe_parts(info.filename)
                    if parts in seen:
                        raise ValueError('Duplicate archive path')
                    seen.add(parts)
                    mode = info.external_attr >> 16
                    if stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR):
                        raise ValueError('Archive links and special files are unsupported')
                    names.append(parts)
                    if not info.is_dir():
                        add(parts, lambda i=info: archive.open(i), info.file_size)
        check_script(names, language)
        if found != set(FILES):
            raise ValueError('Incomplete GOTY package: English US/UK fonts and both menu/item text files are required')
        manifest = dict(format=1, language=language, author='Supuritto', source=SOURCE,
                        files={name: dict(size=(stage / name).stat().st_size,
                                          sha256=digest(stage / name)) for name in FILES})
        (stage / MANIFEST).write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        target = data / f'{language}-{uuid.uuid4().hex}'
        stage.rename(target)
        return inspect(target, language)
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def inspect(folder, language):
    folder = Path(folder)
    if folder.is_symlink():
        raise ValueError('Managed translation folder must not be a symbolic link')
    manifest_path = folder / MANIFEST
    if manifest_path.is_symlink() or manifest_path.stat().st_size > 65536:
        raise ValueError('Invalid translation manifest')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if not isinstance(manifest, dict):
        raise ValueError('Invalid translation manifest')
    if manifest.get('format') != 1 or manifest.get('language') != language:
        raise ValueError('Import the GOTY package for the selected Chinese script')
    if not isinstance(manifest.get('files'), dict) or set(manifest['files']) != set(FILES):
        raise ValueError('Invalid translation asset list')
    actual = set()
    for directory, folders, files in os.walk(folder, followlinks=False):
        for name in [*folders, *files]:
            if (Path(directory) / name).is_symlink():
                raise ValueError('Managed translation contains a symbolic link')
        for name in files:
            actual.add((Path(directory) / name).relative_to(folder).as_posix())
    if actual != {*FILES, MANIFEST}:
        raise ValueError('Managed translation files changed; import the package again')
    for name in FILES:
        path, expected = folder / name, manifest['files'][name]
        if (not isinstance(expected, dict) or not stat.S_ISREG(path.stat().st_mode) or not 0 < path.stat().st_size <= MAX_FILE
                or path.stat().st_size != expected.get('size') or digest(path) != expected.get('sha256')):
            raise ValueError('Translation asset failed integrity checking; import the package again')
    return dict(folder=str(folder.resolve()), valid=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('import', 'inspect'))
    parser.add_argument('--language', required=True, choices=('10', '11'))
    parser.add_argument('--folder', required=True)
    parser.add_argument('--data')
    args = parser.parse_args()
    if args.action == 'import':
        if not args.data:
            parser.error('--data is required for import')
        result = import_package(args.folder, args.data, args.language)
    else:
        result = inspect(args.folder, args.language)
    print(json.dumps(result))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, TypeError, KeyError, zipfile.BadZipFile, RuntimeError) as error:
        print(json.dumps(dict(error=str(error))))
        sys.exit(1)
