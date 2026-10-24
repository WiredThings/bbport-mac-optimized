#!/usr/bin/env python3
"""Associate selected guest API timings with GPU intervals; never assume frame IDs."""
from pathlib import Path
import argparse,collections,hashlib,json,re,statistics
from analyze_stutter import read,union,publication_range
P=Path(__file__).resolve().parents[1]
def frame_cpu_bounds(windows,start,end):
 """Bounds from whole-thread CPU windows; never prorate CPU by wall overlap."""
 selected=sorted((r for r in windows if r['start']<end and r['end']>start),key=lambda r:r['start'])
 intervals=[(max(start,r['start']),min(end,r['end']))for r in selected]
 disjoint=all(a['end']<=b['start']for a,b in zip(selected,selected[1:]))
 sane=all(0<r['wall']and r['cpu']<=r['wall']+1000000 for r in selected)
 covered=union(intervals)
 lower=sum(max(0,r['cpu']-(r['wall']-(b-a))-1000000)for r,(a,b)in zip(selected,intervals))
 upper=sum(min(b-a,r['cpu']+1000000)for r,(a,b)in zip(selected,intervals))
 return dict(windows=len(selected),window_cpu_ms=sum(r['cpu']for r in selected)/1e6,covered_ms=covered/1e6,complete_window_coverage=bool(selected and disjoint and sane and covered==end-start),lower_ms=lower/1e6 if disjoint and sane else None,upper_ms=upper/1e6 if disjoint and sane else None,clock_slack_per_window_ms=1,scope='same thread CPU across retained windows; clipped-frame bounds only if complete coverage, not all game threads or FEX-only cost')

def main():
 ap=argparse.ArgumentParser();ap.add_argument('evidence',type=Path);a=ap.parse_args();e=a.evidence.resolve();assert e.is_relative_to(P/'evidence')
 runtime=json.loads((e/'result.json').read_text());assert runtime['exit']==0 and runtime['stopped']=='time budget','incomplete capture'
 hashes=json.loads((e/'guest-frame-artifacts.json').read_text())
 for f,h in hashes.items():assert Path(f).is_relative_to(P)and hashlib.file_digest(Path(f).open('rb'),'sha256').hexdigest()==h,'artifact changed since capture'
 traces=[read(f)for f in sorted(e.glob('stutter-*.bin'))if f.stem[8:].isdigit()];rows=[r for t in traces for r in t['rows']];trace_by_pid={t['pid']:t for t in traces}
 names={int(op):name for op,name in re.findall(r'BACHATA_FEX_HLE_BEGIN operation=(\d+) name=(\S+)',(e/'runtime.log').read_text())}
 cpu_windows=[r for r in rows if r['kind']==49];spans=[r for r in rows if r['kind']==46];markers=collections.defaultdict(list);statuses=collections.defaultdict(list);accounting={}
 for r in rows:
  key=r['pid'],r['tid']
  if r['kind']==45:markers[key].append(r)
  if r['kind']==47:statuses[key].append(dict(pid=r['pid'],tid=r['tid'],seq=r['seq'],time=r['start'],calls=r['arg']&0xffffffff,invalid=(r['arg']>>32)&0xffff,markers=r['arg']>>48))
  if r['kind']==48:accounting[r['pid'],r['tid'],r['start']]=dict(calls=r['arg']&0xffffffff,completed=r['arg']>>32)
 for table in [markers,statuses]:
  for k,values in table.items():values.sort(key=lambda r:r['start']if'start'in r else r['time'])
 out=dict(authored_trace_fixture=bool(runtime.get('authored_trace_fixture',False)),scope='same-host monotonic selected guest call/frame timing; timestamp association, no frame-ID/causal mapping',artifacts=hashes,trace_quality=[{k:v for k,v in t.items()if k!='rows'}for t in traces],threads=[],long_frames=[])
 for key,points in markers.items():
  gaps=[(b['start']-a['start'])/1e6 for a,b in zip(points,points[1:])];st=statuses[key]
  out['threads'].append(dict(pid=key[0],tid=key[1],markers=len(points),names=sorted({names.get(x['arg'],str(x['arg']))for x in points}),median_gap_ms=statistics.median(gaps)if gaps else None,max_gap_ms=max(gaps)if gaps else None,status_count=len(st),first_status=st[0]if st else None,last_status=st[-1]if st else None,last_accounting=accounting.get((key[0],key[1],st[-1]['time']))if st else None))
 out['frame_cpu_window_statistics']=[]
 for key in markers:
  ws=[r for r in cpu_windows if(r['pid'],r['tid'])==key]
  if ws:
   out['frame_cpu_window_statistics'].append(dict(pid=key[0],tid=key[1],windows=len(ws),median_wall_ms=statistics.median(r['wall']/1e6 for r in ws),max_wall_ms=max(r['wall']/1e6 for r in ws),median_thread_cpu_ms=statistics.median(r['cpu']/1e6 for r in ws),max_thread_cpu_ms=max(r['cpu']/1e6 for r in ws),median_cpu_wall_ratio=statistics.median(r['cpu']/r['wall']for r in ws if r['wall']),first_start_mono_ns=min(r['start']for r in ws),last_end_mono_ns=max(r['end']for r in ws),scope='only retained same-thread marker intervals, not per-frame or whole-process CPU'))
 for frame in sorted((r for r in rows if r['kind']==40 and r['wall']>=40000000),key=lambda r:r['wall'],reverse=True)[:12]:
  hit=[r for r in spans if r['start']<frame['end']and r['end']>frame['start']]
  item=dict(frame_ms=frame['wall']/1e6,start_mono_ns=frame['start'],end_mono_ns=frame['end'],selected_submit_wall_union_ms=union([(max(frame['start'],r['start']),min(frame['end'],r['end']))for r in hit])/1e6,selected_spans=[dict(pid=r['pid'],tid=r['tid'],start_mono_ns=r['start'],end_mono_ns=r['end'],wall_ms=r['wall']/1e6,cpu_ms=r['cpu']/1e6,name=names.get(r['arg'],str(r['arg'])))for r in hit],guest_threads=[])
  for key,points in markers.items():
   before=[r for r in points if r['start']<=frame['start']];inside=[r for r in points if frame['start']<r['start']<frame['end']];after=[r for r in points if r['start']>=frame['end']];adj=before[-1:]+inside+after[:1]
   thread=dict(render_thread_cpu_bounds=frame_cpu_bounds([r for r in cpu_windows if(r['pid'],r['tid'])==key],frame['start'],frame['end']),pid=key[0],tid=key[1],last_before=before[-1]['start']if before else None,inside_marker_times=[r['start']for r in inside],first_after=after[0]['start']if after else None,max_adjacent_marker_gap_ms=max((b['start']-a['start'])/1e6 for a,b in zip(adj,adj[1:]))if len(adj)>1 else None)
   st=statuses[key];prior=[r for r in st if r['time']<=frame['start']];later=[r for r in st if r['time']>=frame['end']]
   if prior and later:
    a,b=prior[-1],later[0];aa=accounting.get((key[0],key[1],a['time']));bb=accounting.get((key[0],key[1],b['time']));saturated=any(x[n]==limit for x in [a,b]for n,limit in [('calls',0xffffffff),('invalid',0xffff),('markers',0xffff)])
    count=sum(a['seq']<r['seq']<b['seq']for r in points);monotonic=all(b[n]>=a[n]for n in ['calls','invalid','markers']);near=b['time']-a['time']<=3000000000
    ended=bool(aa and bb and aa['calls']==aa['completed']==a['calls']and bb['calls']==bb['completed']==b['calls'])
    published=publication_range(trace_by_pid[key[0]],a['seq'],b['seq'])
    thread['coverage']=dict(publication_range=published,before=a,after=b,invalid_delta=b['invalid']-a['invalid'],marker_delta=b['markers']-a['markers'],marker_rows=count,accounting_before=aa,accounting_after=bb,no_open_call_at_report_boundaries=ended,valid=bool(published['valid']and monotonic and near and not saturated and b['invalid']==a['invalid']and count==b['markers']-a['markers']and ended))
   item['guest_threads'].append(thread)
  out['long_frames'].append(item)
 out['all_selected_spans']=[dict(pid=r['pid'],tid=r['tid'],start_mono_ns=r['start'],wall_ms=r['wall']/1e6,cpu_ms=r['cpu']/1e6,name=names.get(r['arg'],str(r['arg'])))for r in sorted(spans,key=lambda r:r['wall'],reverse=True)[:30]]
 out['warning']='Only four named APIs; kind49 uses existing end-clock reads, whole marker intervals include guest/HLE work and waiting; CPU is not prorated to wall time. No baseline FPS claim.  spans under5ms omitted, end markers rate-limited40ms. No-open-accounting and publication checks prove only a retained report interval, never overwritten history or whole execution. CPU time of a full overlapping call is not its clipped frame CPU time. Late marker may be submission wait or prior guest work; regular markers do not alone identify presentation/scheduling cause.'
 (e/'guest-frame-summary.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({k:v for k,v in out.items()if k not in['long_frames','warning','all_selected_spans']},indent=2));print('long frames',len(out['long_frames']),'selected spans',len(spans))
if __name__=='__main__':main()
