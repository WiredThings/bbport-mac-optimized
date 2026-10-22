#!/usr/bin/env python3
"""Analyze bounded command-buffer metadata. Covered intervals are not GPU utilization."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics


def union(intervals):
    total = 0.0
    end = -math.inf
    for a, b in sorted(intervals):
        total += max(0.0, b - max(a, end))
        end = max(end, b)
    return total


def ordered_coverage(intervals):
    """Legacy online high-water algorithm; can miss earlier out-of-order intervals."""
    total = 0.0
    end = -math.inf
    for a, b in intervals:
        if b > end:
            total += b - max(a, end)
            end = b
    return total


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    return dict(mean=statistics.mean(values), median=statistics.median(values),
                p95=values[math.ceil(.95 * len(values)) - 1], max=max(values))


def analyze(path):
    path = Path(path)
    if path.stat().st_size > 512 * 1024:
        raise ValueError('Trace exceeds 512 KiB bound')
    rows = list(csv.DictReader(path.open()))
    if not 1 <= len(rows) <= 2048:
        raise ValueError('Trace has an invalid record count')
    for r in rows:
        for k in ('queue', 'tick', 'frame', 'role'):
            r[k] = int(r[k])
        for k in ('commit_s', 'start_s', 'end_s', 'callback_s'):
            r[k] = float(r[k])
        if r['role'] not in (0, 1) or not all(math.isfinite(r[k]) for k in ('commit_s', 'start_s', 'end_s', 'callback_s')):
            raise ValueError('Invalid queue role or timestamps')
        if not 0 < r['commit_s'] <= r['start_s'] <= r['end_s'] <= r['callback_s']:
            raise ValueError('Invalid command-buffer clock ordering')
    intervals = [(r['start_s'], r['end_s']) for r in rows]
    covered = union(intervals)
    callback_order = sorted(rows, key=lambda r: r['callback_s'])
    online = ordered_coverage([(r['start_s'], r['end_s']) for r in callback_order])
    roles = {}
    for role, name in ((0, 'draw'), (1, 'present')):
        group = [r for r in rows if r['role'] == role]
        frames = sorted(set(r['frame'] for r in group))
        # Ignore the trace's two clipped boundary frames in per-frame aggregates.
        complete = [f for f in frames if frames[0] < f < frames[-1]]
        grouped = [[r for r in group if r['frame'] == f] for f in complete]
        ends = [max(r['end_s'] for r in g) for g in grouped]
        roles[name] = dict(buffers=len(group), queues=len(set(r['queue'] for r in group)),
            complete_frames=len(complete),
            enqueue_to_gpu_ms=distribution([(r['start_s'] - r['commit_s']) * 1000 for r in group]),
            buffer_interval_ms=distribution([(r['end_s'] - r['start_s']) * 1000 for r in group]),
            completion_delivery_ms=distribution([(r['callback_s'] - r['end_s']) * 1000 for r in group]),
            per_frame_covered_ms=distribution([union([(r['start_s'], r['end_s']) for r in g]) * 1000 for g in grouped]),
            per_frame_span_ms=distribution([(max(r['end_s'] for r in g) - min(r['start_s'] for r in g)) * 1000 for g in grouped]),
            completion_spacing_ms=distribution([(b - a) * 1000 for a, b in zip(ends, ends[1:]) if b >= a]))
    return dict(records=len(rows), covered_ms=covered * 1000, legacy_callback_coverage_ms=online * 1000,
                legacy_missed_ms=(covered - online) * 1000, roles=roles,
                notes=['Intervals include dependencies; coverage is not shader work or hardware utilization.',
                       'Draw/present intervals overlap. Role totals and CPU waits must not be added.',
                       'Frame is the host preparation counter; boundary frames excluded; this is not a gameplay FPS score.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    print(json.dumps(analyze(parser.parse_args().trace), indent=2))
