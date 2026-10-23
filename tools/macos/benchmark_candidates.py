#!/usr/bin/env python3
"""Same-binary interleaved A/B of selected candidates. Never enables the normal launcher."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time

spec=importlib.util.spec_from_file_location('benchmark',Path(__file__).with_name('benchmark.py'))
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
CANDIDATES=('BB_MTL_BATCH_CLEARS','BB_MTL_DIRECT_COPY','BB_MTL_ADAPTIVE_STAGING',
            'BB_MTL_TEXTURE_TILING','BB_MTL_PRECISE_RESIDENCY','BB_MTL_COALESCE_BARRIERS',
            'BB_MTL_QUERY_COMPUTE_CHAIN','BB_MTL_PRECISE_COMPUTE','BB_MTL_TEXTURE_TILING_HALF')

def fixed_overrides(items,candidates):
    fixed={}
    for item in items:
        key,sep,value=item.partition('=')
        if not sep or key not in b.OVERRIDES-b.DIAGNOSTIC_OVERRIDES or value not in ('0','1'):
            raise ValueError('Invalid fixed score override: '+item)
        if key in candidates or key in fixed:raise ValueError('Conflicting fixed override: '+key)
        fixed[key]=value
    return fixed

def summarize(path):
    r=json.loads(path.read_text());cleanup=json.loads((path.parent/'cleanup.json').read_text())
    assert r['valid'] and cleanup['own_process_stopped'] and cleanup['original_saves_unchanged']
    samples=[s for s in r['samples'] if s.get('phase')=='score']
    if len(samples)<10:raise ValueError('Insufficient scored memory/session samples')
    mem={}
    for field in ('footprint','owned','metal-allocated','graphics-ledger'):
        vals=[int(m[1])/1024**2 for s in samples for line in s['memory']
              if (m:=re.search(r'\b'+re.escape(field)+r'=(\d+)',line))]
        if not vals:raise ValueError('Missing accounting: '+field)
        mem[field+'_median_mib']=statistics.median(vals)
        mem[field+'_peak_mib']=max(vals)
    if 'BB_METAL_ARM64' in r.get('variants',{}):
        # Renderer ledger excludes the separate Rosetta process in ARM mode.
        # Keep it labelled; never treat that alone as a whole-game RAM saving.
        for suffix in ('median_mib','peak_mib'):
            mem['renderer_process_footprint_'+suffix]=mem.pop('footprint_'+suffix)
        values=[s.get('process_footprint_accounted_sum_bytes') for s in samples]
        if any(v is None for v in values):raise ValueError('Missing split-process memory accounting')
        mem['process_footprint_accounted_sum_median_mib']=statistics.median(values)/1024**2
        mem['process_footprint_accounted_sum_peak_mib']=max(values)/1024**2
    swaps=[s['pressure']['swap_mib'] for s in r['samples']]
    return dict(result=str(path),fps=r['metrics']['fps'],low1=r['metrics']['one_percent_low_fps'],
                p95=r['metrics']['frame_ms_p95'],draws=r['metrics']['draws']['mean'],memory=mem,
                warning_fraction=sum(s['pressure']['level']>=2 for s in samples)/len(samples),
                swap_growth_mib=swaps[-1]-swaps[0],swap_peak_growth_mib=max(swaps)-swaps[0],
                score_swap_growth_mib=samples[-1]['pressure']['swap_mib']-samples[0]['pressure']['swap_mib'],
                renderer=r['renderer_sha256'],probe=r['probe_sha256'],guard=r['guard_policy'],
                fingerprint=r['cohort_fingerprint'],variants=r['variants'],
                visual_observation=r.get('visual_observation','unverified'))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--workspace',type=Path,required=True);p.add_argument('--app',type=Path,required=True)
    p.add_argument('--name',required=True)
    p.add_argument('--order',choices=('ABBA','BAAB'),default='ABBA')
    p.add_argument('--max-swap-growth-mib',type=int,default=b.DEFAULT_SWAP_GROWTH_MIB)
    p.add_argument('--max-swap-total-mib',type=int,default=b.DEFAULT_SWAP_TOTAL_MIB)
    p.add_argument('--swap-budget-baseline-mib',type=float,help='Preserve an earlier baseline across related test suites')
    p.add_argument('--swap-rapid-window-seconds',type=int,choices=(6,30),default=6)
    p.add_argument('--stop-at',type=b.authorization_deadline,help='ISO-8601 test authorization deadline with timezone')
    p.add_argument('--visual-observation',choices=('absent','unverified'),default='unverified',help='Operator declaration, forwarded unchanged to all A/B scoring windows; absent requires no visual capture/observation throughout the suite')
    p.add_argument('--candidate',action='append',choices=sorted(b.OVERRIDES-b.DIAGNOSTIC_OVERRIDES-{'BB_FRAME_TIMING'}),help='Toggle only these flags; default is the accepted nine-flag bundle')
    p.add_argument('--fixed-env',action='append',default=[],help='Keep an allowed non-diagnostic KEY=0/1 constant in both groups')
    args=p.parse_args()
    if not (256<=args.max_swap_growth_mib<=b.DEFAULT_SWAP_GROWTH_MIB and 10240<=args.max_swap_total_mib<=32768):
        p.error('Use swap growth256..12288MiB and absolute ceiling10240..32768MiB')
    candidates=tuple(args.candidate) if args.candidate else CANDIDATES
    if len(set(candidates))!=len(candidates):p.error('Duplicate candidate')
    try:fixed=fixed_overrides(args.fixed_env,candidates)
    except ValueError as error:p.error(str(error))
    if not re.fullmatch('[A-Za-z0-9_-]{1,36}',args.name):p.error('Use a short name')
    out=args.workspace/(args.name+'.json')
    if out.exists():raise ValueError('Refusing to overwrite an existing suite')
    b.check_authorization_deadline(args.stop_at)
    baseline_swap=b.pressure()['swap_mib'] if args.swap_budget_baseline_mib is None else args.swap_budget_baseline_mib
    report=dict(status='running',order=args.order,guard_policy=b.GUARD_POLICY,candidates=list(candidates),fixed_variants=fixed,runs=[],visual_observation=args.visual_observation,visual_observation_source='operator-declaration',max_swap_growth_mib=args.max_swap_growth_mib,max_swap_total_mib=args.max_swap_total_mib,swap_rapid_window_seconds=args.swap_rapid_window_seconds,stop_at=args.stop_at,shared_swap_budget_baseline_mib=baseline_swap,
                installed_app_changed=False,notes=[f'Same-binary {args.order}; A is selected-OFF, B is selected-ON; all other launch defaults retained.',
                'Warning pressure is recorded, not excluded. No frames trimmed.',
                'Visual observation state is an operator declaration; external recorders are not detected. Unverified suites cannot establish capture-free performance.',
                'When several flags are selected, the combined result cannot attribute gains to each flag.'])
    if 'BB_METAL_ARM64' in candidates:
        report['notes'].append('ARM mode: renderer-process footprint excludes the Rosetta game. Accounted footprint sum can include shared accounting; it is not deduplicated physical RAM. Compare system pressure/swap and owned Metal allocations as well.')
    b.write_json(out,report)
    try:
        counts={'a':0,'b':0}
        for group in args.order.lower():
            counts[group]+=1;label=group+str(counts[group]);value='0' if group=='a' else '1'
            b.check_authorization_deadline(args.stop_at)
            current=b.pressure()
            b.MemoryGuard(baseline_swap,args.max_swap_growth_mib,args.max_swap_total_mib,args.swap_rapid_window_seconds).check(current,time.monotonic())
            name=args.name+'-'+label
            report.update(active=name);b.write_json(out,report)
            cmd=[sys.executable,str(Path(__file__).with_name('benchmark.py')),'run','--workspace',str(args.workspace),
                 '--app',str(args.app),'--name',name,'--mute','--minimal-stats','--visual-observation',args.visual_observation,
                 '--max-swap-growth-mib',str(args.max_swap_growth_mib),'--max-swap-total-mib',str(args.max_swap_total_mib),'--swap-budget-baseline-mib',str(baseline_swap),'--swap-rapid-window-seconds',str(args.swap_rapid_window_seconds)]
            if args.stop_at is not None:cmd+=['--stop-at',b.datetime.fromtimestamp(args.stop_at).astimezone().isoformat()]
            for key in candidates:cmd+=['--env',key+'='+value]
            for key,value_ in fixed.items():cmd+=['--env',key+'='+value_]
            if 'BB_MTL_FRAGMENT_UNIFORMS' not in candidates and 'BB_MTL_FRAGMENT_UNIFORMS' not in fixed:cmd+=['--env','BB_MTL_FRAGMENT_UNIFORMS=0']
            print('Starting '+name+' selected candidates='+value,flush=True)
            with (args.workspace/(name+'.console.log')).open('w') as log:
                done=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,env=os.environ)
            if done.returncode:raise RuntimeError('Run failed: '+name+'; see console log')
            paths=list((args.workspace/'runs').glob('*-'+name+'/result.json'))
            if len(paths)!=1:raise RuntimeError('Missing or ambiguous run: '+name)
            summary=summarize(paths[0]);summary['label']=label;report['runs'].append(summary)
            b.write_json(out,report);print(json.dumps(summary),flush=True)
            # Wait for owned resources to retire; no other application is touched.
            time.sleep(10)
        raw=[json.loads(Path(r['result']).read_text()) for r in report['runs']]
        for index,r in enumerate(raw[1:],1):
            from contextlib import redirect_stdout
            import io
            with redirect_stdout(io.StringIO()):b.compare(argparse.Namespace(before=Path(report['runs'][0]['result']),after=Path(report['runs'][index]['result'])))
        groups={v:[r for r in report['runs'] if r['label'].startswith(v)] for v in ('a','b')}
        report['means']={v:{key:statistics.mean(r[key] for r in group) for key in ('fps','low1','p95','warning_fraction','swap_growth_mib','score_swap_growth_mib')} |
                         {'memory':{key:statistics.mean(r['memory'][key] for r in group) for key in group[0]['memory']}}
                         for v,group in groups.items()}
        report['fps_delta_percent']=(report['means']['b']['fps']/report['means']['a']['fps']-1)*100
        report.update(status='complete',active=None);b.write_json(out,report)
        print(json.dumps(report['means'],indent=2),flush=True)
    except BaseException as error:
        report.update(status='failed',error=str(error));b.write_json(out,report);raise

if __name__=='__main__':main()
