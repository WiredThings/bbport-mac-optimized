#!/usr/bin/env python3
"""Summarize memory categories without adding overlapping aliases/subsets to footprint."""
import argparse
import json
from pathlib import Path
import re


def fields(text):
    return {k: int(v) for k, v in re.findall(r'([a-z][a-z0-9-]*)=(\d+)', text)}


def analyze(text):
    snapshots, categories = [], {}
    for line in text.splitlines():
        if 'Metal memory category ' in line:
            tail = line.split('Metal memory category ', 1)[1]
            name, values = tail.split(': ', 1)
            categories[name] = fields(values)
        elif 'Metal memory totals: ' in line:
            snapshots.append({'categories': categories, 'totals': fields(line.split('Metal memory totals: ', 1)[1])})
            categories = {}
        elif 'Guest memory categories: ' in line and snapshots:
            snapshots[-1]['guest'] = fields(line.split('Guest memory categories: ', 1)[1])
    if not snapshots:
        raise ValueError('No categorized memory snapshots; run the 0.2.6 Metal backend first')
    first, last = snapshots[0], snapshots[-1]
    guest = last.get('guest', {})
    totals = last['totals']
    return {'snapshot_count': len(snapshots), 'unit': 'byte', 'latest': last,
            'process_footprint_gib': totals.get('footprint', 0) / 1024**3,
            'footprint_change_bytes': totals.get('footprint', 0) - first['totals'].get('footprint', 0),
            'categories_by_capacity': sorted(last['categories'], key=lambda k: last['categories'][k].get('allocated', 0), reverse=True),
            'direct_liveness_available': 'direct-live' in guest,
            'direct_release_calls_since_first_snapshot': guest.get('direct-release-calls', 0) - first.get('guest', {}).get('direct-release-calls', 0),
            'notes': ['Allocated is logical Metal allocation capacity, not resident bytes.',
                      'Retired and discarded-capacity are subsets of allocated, not additional memory.',
                      'Guest aliases, live guest allocations and graphics ledger overlap process footprint.',
                      'Released counts Metal object destruction; the driver may retain/reuse physical pages.',
                      'Unclassified Metal capacity includes opaque framework and driver allocations.',
                      'Runtime direct liveness does not measure suballocations inside the game engine heap.']}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log', type=Path)
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    result = analyze(args.log.read_text(errors='replace'))
    data = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(data)
    print(data, end='')


if __name__ == '__main__':
    main()
