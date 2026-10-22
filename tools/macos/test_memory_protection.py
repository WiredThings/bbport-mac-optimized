#!/usr/bin/env python3
"""Check guest protection/trap composition without running the game."""
from __future__ import annotations

import argparse
import itertools
import json
from pathlib import Path
import platform
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, help='Optional JSON result file')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='blood-echoes-memory-protection-') as folder:
        binary = Path(folder) / 'test-memory-protection'
        command = ['clang', '-std=gnu11', '-O1', '-g', '-pthread']
        if platform.system() == 'Darwin':
            command += ['-arch', 'x86_64']
        command += ['-I' + str(root / 'src'),
                    str(root / 'tests/test_runtime_memory_protection.c'),
                    str(root / 'src/low_heap.c'), '-o', str(binary)]
        subprocess.run(command, check=True)
        if platform.system() == 'Darwin':
            subprocess.run(['codesign', '--force', '--sign', '-', str(binary)], check=True)
            subprocess.run(['codesign', '--verify', '--strict', str(binary)], check=True)
        # None/read/read-write; trap owners alone and together. Neighboring pages,
        # removing one owner, and removing all owners are checked in each case.
        cases = list(itertools.product((0, 1, 0x33), (0, 1, 0x33),
                                       (0, 1, 2, 3, 16, 32, 34)))
        # RX mappings: validate CPU data permissions, not instruction execution.
        cases += [(old, new, reason) for old, new in ((5, 0), (0, 5), (5, 1), (1, 5))
                  for reason in (0, 2, 34)]
        rows = []
        for old, new, reason in cases:
            result = subprocess.run([str(binary), str(old), str(new), str(reason)],
                                    capture_output=True, text=True, timeout=10)
            rows.append(dict(old=old, new=new, reason=reason, exit=result.returncode,
                             stdout=result.stdout, stderr=result.stderr))
    failed = [row for row in rows if row['exit'] != 0]
    report = dict(cases=rows, count=len(rows), failed=len(failed),
                  scope='Real runtime, CPU data permissions; no instruction execution or GPU work')
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f"Guest protection/trap composition: {len(rows)} cases, {len(failed)} failures")
    for row in failed:
        print(json.dumps(row))
    return bool(failed)


if __name__ == '__main__':
    raise SystemExit(main())
