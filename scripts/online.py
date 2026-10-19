#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write credential-free THD-compatible routes outside the shared save directory."""
import argparse
import json
import os
from pathlib import Path
import tempfile
from urllib.parse import urlsplit


def origin(value):
    parts = urlsplit(value)
    if (parts.scheme != 'https' or not parts.hostname or parts.username is not None or
            parts.password is not None or parts.path not in ('', '/') or parts.query or parts.fragment):
        raise ValueError('Use HTTPS service origins without a path, password or query')
    if parts.port is not None and not 1 <= parts.port <= 65535:
        raise ValueError('Invalid service port')
    return value.rstrip('/')


def prepare(user, game='https://thehuntersdream.com', session='https://thehuntersdream.com:18671', output=None):
    user = Path(user)
    if user.is_symlink() or not user.is_dir():
        raise ValueError('Unsupported save directory')
    game, session = origin(game), origin(session)
    overrides = {'*.scej-network.jp': game}
    overrides.update({host: session for host in (
        'bb-playlog-prod.s3.amazonaws.com', 'bb-playlog-test.s3.amazonaws.com')})
    target = Path(output) if output is not None else user
    if target.is_symlink():
        raise ValueError('Unsupported route directory')
    target.mkdir(parents=True, exist_ok=True)
    path = target / 'host_overrides.json'
    if path.is_symlink():
        raise ValueError('Unsupported host override link')
    fd, temporary = tempfile.mkstemp(prefix='.host-overrides-', dir=target)
    try:
        with os.fdopen(fd, 'w') as stream:
            json.dump(overrides, stream, indent=2)
            stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary): os.unlink(temporary)
    return path.resolve()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--user', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(prepare(args.user, os.environ.get('BB_THD_HTTP_ROUTE', os.environ.get('BB_THD_HTTP_ORIGIN', 'https://thehuntersdream.com')),
                      os.environ.get('BB_THD_SERVER', 'https://thehuntersdream.com:18671'), args.output))
    except (OSError, ValueError) as error:
        parser.exit(1, f'STOP: {error}\n')
