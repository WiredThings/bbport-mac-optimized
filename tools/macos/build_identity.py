#!/usr/bin/env python3
"""Identify this source tree, including Git-less corresponding-source exports."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def valid_revision(value):
    return value.lower() if isinstance(value, str) and re.fullmatch(r"(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})", value) else None


def manifest_revision(value):
    if isinstance(value, dict):
        return valid_revision(value.get('source_commit'))
    if isinstance(value, list):
        for item in value:
            if isinstance(item, dict) and item.get('component') == 'blood-echoes':
                return valid_revision(item.get('revision'))
    return None


def source_revision(root):
    root = Path(root).resolve()
    # Do not accidentally report a parent repository when building an export.
    result = subprocess.run(['git', '-C', str(root), 'rev-parse', '--show-toplevel'], text=True, capture_output=True)
    if result.returncode == 0 and Path(result.stdout.strip()).resolve() == root:
        revision = subprocess.run(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True, capture_output=True)
        if revision.returncode == 0:
            return valid_revision(revision.stdout.strip())
    try:
        return manifest_revision(json.loads((root / 'SOURCE-MANIFEST.json').read_text()))
    except (OSError, ValueError):
        return None


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--required', action='store_true')
    args = parser.parse_args()
    revision = source_revision(args.root)
    if revision:
        print(revision)
    elif args.required:
        parser.exit(1, 'STOP: source revision unavailable; use a Git checkout or a verified source export.\n')
