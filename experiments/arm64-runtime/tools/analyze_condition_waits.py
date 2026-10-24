#!/usr/bin/env python3
"""Associate private render-thread condition waits with matching API calls, not causality."""
from pathlib import Path
import argparse,collections,hashlib,json,re
from analyze_stutter import read,union,publication_range
from analyze_guest_frames import frame_cpu_bounds
P=Path(__file__).resolve().parents[1];MAX_TOKEN=(1<<30)-1

def main():
 ap=argparse.ArgumentParser();ap.add_argument('evidence',type=Path);a=ap.parse_args();e=a.evidence.resolve();assert e.is_relative_to(P/'evidence')
 runtime=json.loads((e/'result.json').read_text());assert runtime['exit']==0 and runtime['stopped']=='time budget','incomplete capture'
 hashes=json.loads((e/'guest-frame-artifacts.json').read_text())
 for f,h in hashes.items():assert Path(f).is_relative_to(P)and hashlib.file_digest(Path(f).open('rb'),'sha256').hexdigest()==h,'artifact changed since capture'
 traces=[read(f)for f in sorted(e.glob('stutter-*.bin'))if f.stem[8:].isdigit()];trace_by_pid={t['pid']:t for t in traces};rows=[r for t in traces for r in t['rows']];names={int(op):name for op,name in re.findall(r'BACHATA_FEX_HLE_BEGIN operation=(\d+) name=(\S+)',(e/'runtime.log').read_text())}
 meta=collections.defaultdict(list);status=collections.defaultdict(list);errors={}
 for r in rows:
  if r['kind']in [51,52,53,55]:meta[r['pid'],r['tid'],r['start'],r['kind']].append(r)
  if r['kind']==56:status[r['pid'],r['tid']].append(r)
  if r['kind']==57:errors[r['pid'],r['tid'],r['start']]=r
 for values in status.values():values.sort(key=lambda r:r['start'])
 waits=[];bad=[]
 for w in sorted((r for r in rows if r['kind']==50),key=lambda r:r['wall'],reverse=True):
  parts={k:meta[w['pid'],w['tid'],w['start'],k]for k in [51,52,53,55]}
  unique=all(len(v)==1 for v in parts.values());sane=0<w['arg']<=MAX_TOKEN and w['wall']>=20000000 and w['cpu']<=w['wall']+1000000
  item=dict(pid=w['pid'],tid=w['tid'],token=w['arg'],start_mono_ns=w['start'],end_mono_ns=w['end'],wall_ms=w['wall']/1e6,cpu_ms=w['cpu']/1e6,metadata_complete=unique,sane=sane,wakes=[])
  if unique:item.update(condition_slot=parts[51][0]['arg'],mutex_slot=parts[52][0]['arg'],result=parts[53][0]['arg'],operation=names.get(parts[55][0]['arg'],str(parts[55][0]['arg'])))
  else:bad.append(dict(pid=w['pid'],token=w['arg'],reason='missing or duplicate condition metadata'))
  for s in (r for r in rows if r['kind']==54 and r['pid']==w['pid']and(r['arg']&MAX_TOKEN)==w['arg']):
   stage=(s['arg']>>30)&3;result=s['arg']>>32;valid=stage in [2,3]and s['cpu']<=s['wall']+1000000 and s['start']<=w['end']+1000000 and s['end']>=w['start']-1000000
   item['wakes'].append(dict(tid=s['tid'],seq=s['seq'],kind={2:'signal',3:'broadcast'}.get(stage,'invalid'),result=result,start_mono_ns=s['start'],end_mono_ns=s['end'],api_wall_ms=s['wall']/1e6,api_cpu_ms=s['cpu']/1e6,after_wait_entry_ms=(s['start']-w['start'])/1e6,wait_return_after_api_end_ms=(w['end']-s['end'])/1e6,valid=valid))
  for wake in item['wakes']:
   producer=[r for r in rows if r['kind']==58 and r['pid']==w['pid']and r['tid']==wake['tid']and unique and r['arg']==item['condition_slot']]
   if wake['end_mono_ns']>w['start']:wake['producer_cpu_before_notification']=frame_cpu_bounds(producer,w['start'],wake['end_mono_ns'])
  before=[r for r in status[w['pid'],w['tid']]if r['start']<=w['start']];after=[r for r in status[w['pid'],w['tid']]if r['start']>=w['end']]
  if before and after:
   x,y=before[-1],after[0];ex=errors.get((w['pid'],w['tid'],x['start']));ey=errors.get((w['pid'],w['tid'],y['start']));xc=x['arg']&0xffffffff;xd=x['arg']>>32;yc=y['arg']&0xffffffff;yd=y['arg']>>32
   published=publication_range(trace_by_pid[w['pid']],x['seq'],y['seq']);near=y['start']-x['start']<=3000000000;saturated=0xffffffff in [xc,xd,yc,yd]or any(v==0xffffffff for r in [ex,ey]if r for v in [r['arg']&0xffffffff,r['arg']>>32]);closed=xc==xd and yc==yd and yc>=xc;no_errors=bool(ex and ey and ex['arg']==ey['arg'])
   wake_publication=all(x['seq']<=s['seq']<=y['seq']for s in item['wakes'])
   item['coverage']=dict(valid=bool(unique and sane and published['valid']and near and not saturated and closed and no_errors and wake_publication and all(s['valid']for s in item['wakes'])),publication_range=published,nearby_reports=near,no_open_calls=closed,errors_unchanged=no_errors,error_count_before=ex['arg']if ex else None,error_count_after=ey['arg']if ey else None,matched_wakes_inside_publication_bracket=wake_publication,warning='only admitted same-thread waits and matching argument-slot wake calls; aliases, native direct wakes and spurious wakes remain unknown')
  waits.append(item)
 frame_summary=[]
 for f in sorted((r for r in rows if r['kind']==40 and r['wall']>=40000000),key=lambda r:r['wall'],reverse=True)[:12]:
  hit=[w for w in waits if w['start_mono_ns']<f['end']and w['end_mono_ns']>f['start']]
  frame_summary.append(dict(frame_ms=f['wall']/1e6,start_mono_ns=f['start'],condition_wait_union_ms=union([(max(f['start'],w['start_mono_ns']),min(f['end'],w['end_mono_ns']))for w in hit])/1e6,wait_tokens=[dict(pid=w['pid'],token=w['token'],coverage_valid=w.get('coverage',{}).get('valid',False))for w in hit]))
 out=dict(producer_cpu_windows=sum(r['kind']==58 for r in rows),generic_condition_wait_scopes_suppressed=bool(json.loads((e/'stutter-case-conditions.json').read_text()).get('generic_condition_wait_scopes_suppressed',False))if(e/'stutter-case-conditions.json').exists()else False,authored_trace_fixture=bool(runtime.get('authored_trace_fixture',False)),scope='selected render-thread condition slots, opaque identities only; API association not proof of the waking thread or predicate',artifact_sha256=hashes,trace_quality=[{k:v for k,v in t.items()if k!='rows'}for t in traces],waits=waits,metadata_rejections=bad,long_frames=frame_summary,warnings=['Condition slot aliases are not resolved; no matching wake is not proof of no notification','Wait includes native condition wait and mutex reacquisition; no priority/scheduler/lock semantics changed','Multiple successful signal/broadcast calls may target different waiters; association cannot prove which call woke this thread','Fixed8 slot/token limit or bad clock increments error counts; incomplete traces/counters cannot rule out unobserved calls','Clocks and diagnostic writes can perturb scheduling; compare with all-diagnostics-off control'])
 (e/'condition-wait-summary.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(dict(trace_quality=out['trace_quality'],waits=len(waits),metadata_rejections=bad,long_frames=frame_summary),indent=2))
if __name__=='__main__':main()
