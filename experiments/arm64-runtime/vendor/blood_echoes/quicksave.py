#!/usr/bin/env python3
"""Verified Bloodborne checkpoints. Live saves call this only with all save mounts closed.

Restore/recovery run before the guest starts. Files remain ordinary Bloodborne saves;
this does not capture emulator memory. Every restore retains the displaced save.
"""
import argparse
import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import time
import uuid


def sync_dir(path):
    fd = os.open(path, os.O_RDONLY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def atomic_json(path, value):
    tmp = path.with_name(path.name + '.tmp')
    with tmp.open('w') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(tmp, path)
    sync_dir(path.parent)


def status(message):
    try:
        print('Quicksave: ' + message, flush=True)
    except OSError:
        pass  # A closed launcher log must not invalidate a completed checkpoint.
    target = os.environ.get('BB_QUICKSAVE_STATUS')
    if target:
        path = Path(target)
        tmp = path.with_name(path.name + '.tmp')
        try:
            tmp.write_text(f'{int(time.time()) + 8}\n{message}\n')
            os.replace(tmp, path)
        except OSError:
            pass


def inventory(path):
    if path.is_symlink() or not path.is_dir():
        raise ValueError('Save directory is missing or is a symlink')
    result = {}
    for p in sorted(path.rglob('*')):
        if p.is_symlink():
            raise ValueError('Save contains a symlink')
        if p.name.endswith('.bbtmp'):
            raise ValueError('A save write is unfinished; try again after the next autosave')
        if p.is_dir() or p.name == '.DS_Store':
            continue
        if not p.is_file():
            raise ValueError('Save contains a non-regular file')
        result[str(p.relative_to(path))] = {
            'size': p.stat().st_size,
            'sha256': hashlib.sha256(p.read_bytes()).hexdigest(),
        }
    for name in ('SPRJ0005/userdata0000', 'SPRJ0005/userdata0010'):
        if not result.get(name, {}).get('size'):
            raise ValueError('No Bloodborne character save exists yet')
    return result


def copy_verified(source, target, expected):
    shutil.copytree(source, target, ignore=shutil.ignore_patterns('.DS_Store'))
    if inventory(target) != expected or inventory(source) != expected:
        raise ValueError('Save changed while copying; checkpoint was not installed')
    for p in target.rglob('*'):
        if p.is_file():
            with p.open('rb') as stream:
                os.fsync(stream.fileno())
    for p in sorted(target.rglob('*'), reverse=True):
        if p.is_dir():
            sync_dir(p)
    sync_dir(target)


class Checkpoints:
    def __init__(self, user, title):
        if not re.fullmatch(r'CUSA\d{5}', title):
            raise ValueError('Invalid game title')
        self.user = Path(user).resolve(strict=True)
        self.live = self.user / 'savedata' / '1' / title
        self.root = self.user / 'quicksaves' / title
        for p in (self.user / 'savedata', self.live.parent, self.live,
                  self.user / 'quicksaves', self.root):
            if p.is_symlink():
                raise ValueError('Save paths must not be symlinks')
        self.root.mkdir(parents=True, exist_ok=True)
        self.journal = self.root / 'restore.json'

    @contextlib.contextmanager
    def locked(self):
        with (self.root / 'lock').open('a') as stream:
            fcntl.flock(stream, fcntl.LOCK_EX)
            yield

    def recover(self):
        if not self.journal.exists():
            return
        record = json.loads(self.journal.read_text())
        name = record['backup']
        if not re.fullmatch(r'before-load-[0-9a-f]{32}', name):
            raise ValueError('Invalid restore journal; saves were not changed')
        backup = self.root / name / 'data'
        if not self.live.exists():
            if inventory(backup) != record['before']:
                raise ValueError('Interrupted restore backup failed verification')
            backup.rename(self.live)
            sync_dir(self.live.parent)
            status('Interrupted load recovered; previous progress restored')
        elif inventory(self.live) not in (record['before'], record['after']):
            raise ValueError('Save changed after an interrupted restore; manual recovery needed')
        self.journal.unlink()
        sync_dir(self.root)

    def save(self):
        self.recover()
        expected = inventory(self.live)
        name = 'save-' + uuid.uuid4().hex
        folder = self.root / name
        folder.mkdir()
        copy_verified(self.live, folder / 'data', expected)
        atomic_json(folder / 'manifest.json', {'files': expected, 'created': time.time()})
        atomic_json(self.root / 'current.json', {'checkpoint': name})
        status('Quicksaved - checkpoint ready')
        return folder

    def check(self):
        pointer = self.root / 'current.json'
        if not pointer.exists():
            raise ValueError('No quicksave yet - save a checkpoint first')
        name = json.loads(pointer.read_text())['checkpoint']
        if not re.fullmatch(r'save-[0-9a-f]{32}', name):
            raise ValueError('Invalid checkpoint reference')
        folder = self.root / name
        expected = json.loads((folder / 'manifest.json').read_text())['files']
        if inventory(folder / 'data') != expected:
            raise ValueError('Quicksave failed verification; current progress is untouched')
        return folder, expected

    def load(self):
        if os.environ.get('BB_ONLINE') == '1':
            raise ValueError('Quickload requires an offline restart; saves were not changed')
        marker = self.user / '.quickload-offline.json'
        if marker.is_symlink():
            raise ValueError('Offline restore marker must not be a symlink')
        # Durable before any recovery/restore mutation: the launcher may have crashed.
        atomic_json(marker, {'version': 1})
        self.recover()
        folder, expected = self.check()
        before = inventory(self.live)
        name = 'before-load-' + uuid.uuid4().hex
        backup = self.root / name
        backup.mkdir()
        atomic_json(backup / 'manifest.json', {'files': before, 'created': time.time()})
        stage = self.root / ('restore-' + uuid.uuid4().hex)
        copy_verified(folder / 'data', stage, expected)
        atomic_json(self.journal, {'backup': name, 'before': before, 'after': expected})
        self.live.rename(backup / 'data')
        sync_dir(backup)
        sync_dir(self.live.parent)
        try:
            stage.rename(self.live)
            sync_dir(self.live.parent)
            if inventory(self.live) != expected:
                raise ValueError('Restored save failed verification')
        except BaseException:
            if self.live.exists():
                self.live.rename(backup / 'failed-restore')
            (backup / 'data').rename(self.live)
            sync_dir(self.live.parent)
            self.journal.unlink()
            raise
        self.journal.unlink()
        sync_dir(self.root)
        status('Quicksave restored - loading game offline')
        return backup


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('save', 'check', 'load', 'recover'))
    parser.add_argument('user')
    parser.add_argument('title')
    args = parser.parse_args()
    try:
        saves = Checkpoints(args.user, args.title)
        with saves.locked():
            getattr(saves, args.action)()
    except (OSError, ValueError, KeyError, TypeError) as error:
        status(str(error))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
