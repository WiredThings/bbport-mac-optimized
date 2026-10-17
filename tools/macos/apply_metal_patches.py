#!/usr/bin/env python3
"""Apply an ordered patch series only to an exact known prefix, preserving unrelated edits.

Later patches may change earlier hunks. Reconstruct every prefix from HEAD in a temporary
folder instead of testing each patch in reverse independently against the final source tree.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile


def run(*args, cwd):
    return subprocess.run(args, cwd=cwd, check=True, capture_output=True).stdout


def apply_series(source, patches):
    source = Path(source).resolve()
    patches = [Path(p).resolve() for p in patches]
    paths = set()
    for patch in patches:
        for line in run('git', 'apply', '--numstat', str(patch), cwd=source).decode().splitlines():
            name = line.split('\t', 2)[2]
            path = Path(name)
            if path.is_absolute() or '..' in path.parts or '=>' in name:
                raise ValueError(f'Unsupported patch path: {name}')
            paths.add(path)
    def content(root, path):
        target = root / path
        if any(parent.is_symlink() for parent in [target, *target.parents] if parent != root.parent):
            raise ValueError(f'Symlink in patch path: {target}')
        return target.read_bytes() if target.is_file() else None
    current = {p: content(source, p) for p in paths}
    matched = None
    with tempfile.TemporaryDirectory(prefix='bbport-metal-series-') as directory:
        scratch = Path(directory).resolve()
        for path in paths:
            blob = subprocess.run(['git', 'show', f'HEAD:{path.as_posix()}'], cwd=source, capture_output=True)
            if blob.returncode == 0:
                target = scratch / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(blob.stdout)
        for index in range(len(patches) + 1):
            if all(content(scratch, p) == current[p] for p in paths): matched = index
            if index < len(patches):
                run('git', 'apply', str(patches[index]), cwd=scratch)
    if matched is None:
        raise RuntimeError('Metal patch targets contain unrecognized edits; refusing to overwrite them')
    for patch in patches[matched:]:
        run('git', 'apply', '--check', str(patch), cwd=source)
        run('git', 'apply', str(patch), cwd=source)
    return matched, len(patches)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('patches', type=Path, nargs='+')
    args = parser.parse_args()
    old, total = apply_series(args.source, args.patches)
    print(f'Metal patch series: prefix {old}/{total}; now {total}/{total}')


if __name__ == '__main__':
    main()
