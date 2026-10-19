#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare The Hunter's Dream routing in an explicitly isolated online-save directory.

No credentials are written. Offline launches never invoke this helper.
"""
import argparse
import json
import os
from pathlib import Path
import tempfile

MARKER = '.blood-echoes-online-save'
MARKER_TEXT = 'Blood Echoes isolated online saves v1\n'


def prepare(user):
    user = Path(user)
    if user.is_symlink() or not user.is_dir() or (user / MARKER).is_symlink():
        raise ValueError('Online play requires its own isolated save copy')
    if (user / MARKER).read_text() != MARKER_TEXT:
        raise ValueError('Online save marker is invalid')
    base = 'https://thehuntersdream.com'
    # Wildcard matches all region-specific check-in hosts. The community answers the same
    # game protocol; its HTTP layer adds X-BB-Token only for these redirected game requests.
    overrides = {'*.scej-network.jp': base}
    overrides.update({host: base + ':18671' for host in (
        'bb-playlog-prod.s3.amazonaws.com', 'bb-playlog-test.s3.amazonaws.com')})
    path = user / 'host_overrides.json'
    if path.is_symlink():
        raise ValueError('Unsupported host override link')
    fd, temporary = tempfile.mkstemp(prefix='.host-overrides-', dir=user)
    try:
        with os.fdopen(fd, 'w') as stream:
            json.dump(overrides, stream, indent=2)
            stream.write('\n')
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return path.resolve()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--user', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(prepare(args.user))
    except (OSError, ValueError) as error:
        parser.exit(1, f'STOP: {error}\n')
