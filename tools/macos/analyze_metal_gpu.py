#!/usr/bin/env python3
"""Summarize sampled Metal encoder spans; never equate their sum with frame time."""
import argparse
import json
from pathlib import Path
import re


def fields(line):
    return {k: float(v) for k, v in re.findall(r'([a-z][a-z0-9/-]*)=(-?\d+(?:\.\d+)?)', line)}


def analyze(text, skip_windows=0):
    windows = []
    for line in text.splitlines():
        if 'Metal GPU timing: seconds=' in line:
            windows.append(dict(fields(line), stages={}, transitions={}, hot_passes=[]))
        elif 'Metal GPU stage: name=' in line and windows:
            name = re.search(r'name=([a-z-]+)', line)[1]
            windows[-1]['stages'][name] = fields(line)
        elif 'Metal GPU hot pass: label=' in line and windows:
            label = re.search(r'label=(.*?), samples=', line)[1]
            windows[-1]['hot_passes'].append(dict(fields(line), label=label))
        elif 'Metal GPU hot pass overflow:' in line and windows:
            windows[-1]['hot_dropped'] = fields(line).get('samples', 0)
        elif 'Metal GPU transitions:' in line and windows:
            windows[-1]['transitions'] = {k: int(v) for k, v in re.findall(r'(\d>\d)=(\d+)', line)}
    selected = [w for w in windows[skip_windows:] if w.get('frames', 0) > 0]
    if not selected:
        raise ValueError('No completed timing windows with game frames')
    frames = sum(w['frames'] for w in selected)
    stages = {}
    for w in selected:
        for name, values in w['stages'].items():
            stage = stages.setdefault(name, dict(samples=0, sampled_ms=0, weighted_ms=0))
            stage['samples'] += values['samples']
            stage['sampled_ms'] += values['sampled-ms']
            stage['weighted_ms'] += values['sampled-ms'] * w['every']
    hot_passes = {}
    for w in selected:
        for h in w['hot_passes']:
            entry = hot_passes.setdefault(h['label'], dict(samples=0, weighted_ms=0))
            entry['samples'] += h['samples']
            entry['weighted_ms'] += h['sampled-ms'] * w['every']
    for h in hot_passes.values():
        h['estimated_ms_per_frame'] = h['weighted_ms'] / frames
    for stage in stages.values():
        stage['estimated_ms_per_frame'] = stage['weighted_ms'] / frames
        stage['mean_us_per_encoder'] = stage['sampled_ms'] * 1000 / stage['samples']
    return dict(windows=selected, frames=frames,
                hot_passes_by_span=sorted(hot_passes.items(), key=lambda x: x[1]['estimated_ms_per_frame'], reverse=True),
                hot_pass_dropped=sum(w.get('hot_dropped', 0) for w in selected),
                stages_by_span=sorted(stages.items(), key=lambda x: x[1]['estimated_ms_per_frame'], reverse=True),
                cpu_boundary_ms_per_frame=sum(w['cpu-boundary-ms'] for w in selected) / frames,
                cpu_commit_ms_per_frame=sum(w['cpu-commit-ms'] for w in selected) / frames,
                encoders_per_frame=sum(w['encoders'] for w in selected) / frames,
                barriers_per_frame=sum(w['barriers'] for w in selected) / frames,
                invalid=sum(w['invalid'] for w in selected),
                missing=sum(w.get('missing', 0) for w in selected),
                partial_render=sum(w.get('partial-render', 0) for w in selected),
                capacity_drops=sum(w['capacity-drops'] for w in selected),
                busy_drops=sum(w['busy-drops'] for w in selected),
                notes=['GPU stage spans can overlap; their sum is not GPU frame time.',
                       'Hot passes report the first pipeline of each pass, not isolated shader costs; only the top eight labels per window are printed.',
                       'Per-frame costs are sampling estimates; MetalFX boundary spans include two diagnostic probes and their fence dependency.',
                       'Uncovered command-buffer time includes dependencies, tile work and uninstrumented work; it is not a direct measurement of switching cost.',
                       'CPU boundary time measures native encoder creation/closing and setup, not all CPU draw encoding.',
                       'Presentation uses a separate queue and is excluded from these draw-queue stages.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--skip-windows', type=int, default=0)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = json.dumps(analyze(args.log.read_text(errors='replace'), args.skip_windows), indent=2) + '\n'
    if args.output:
        args.output.write_text(report)
    print(report, end='')
