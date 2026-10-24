#!/usr/bin/env python3
"""Offline fixed-trace coverage and same-build task offsets, no game execution."""
from pathlib import Path
import argparse,bisect,collections,hashlib,json,re,subprocess
from analyze_stutter import read
P=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('evidence',type=Path);a=ap.parse_args();e=a.evidence.resolve()
 assert e.is_relative_to(P/'evidence') and (e/'result.json').exists(),'finished Shared case required'
 run=json.loads((e/'result.json').read_text());assert run['exit']==0 and run['stopped']=='time budget','incomplete run is not coverage proof'
 artifact=json.loads((e/'packet-audit-artifacts.json').read_text());library=P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib'
 assert Path(artifact['gpu_library']).resolve()==library and hashlib.file_digest(library.open('rb'),'sha256').hexdigest()==artifact['sha256'],'library changed; do not resolve against a different binary'
 text=subprocess.check_output(['/usr/bin/otool','-l',str(library)],text=True);match=re.findall(r'segname __TEXT\s+vmaddr (0x[0-9a-fA-F]+)\s+vmsize (0x[0-9a-fA-F]+)',text);assert len(match)==1
 preferred,size=map(lambda x:int(x,16),match[0]);symbols=[]
 proc=subprocess.Popen(['/usr/bin/nm','-an',str(library)],stdout=subprocess.PIPE,text=True)
 for line in proc.stdout:
  m=re.match(r'^([0-9a-fA-F]+) [tT] (.+)$',line.strip())
  if m:symbols.append((int(m[1],16),m[2]))
 assert proc.wait()==0;symbols.sort();addresses=[x[0]for x in symbols];assert addresses
 traces=[read(f)for f in sorted(e.glob('stutter-*.bin'))if f.stem[8:].isdigit()];rows=[r for t in traces for r in t['rows']]
 trace_quality={t['pid']:{k:v for k,v in t.items()if k!='rows'}for t in traces};packets=[r for r in rows if r['kind']==17];windows=[r for r in rows if r['kind']==18];identity=[r for r in rows if r['kind']==43];targets=[r for r in rows if r['kind']==44]
 statuses=collections.defaultdict(list)
 for r in rows:
  if r['kind']==19:statuses[r['pid'],r['tid']].append(dict(pid=r['pid'],tid=r['tid'],seq=r['seq'],mono_ns=r['start'],anchors=r['arg']&0xffffffff,slow=(r['arg']>>32)&0xffff,rejected=(r['arg']>>48)&0xffff))
 for values in statuses.values():values.sort(key=lambda x:x['mono_ns'])
 out=dict(authored_trace_fixture=bool(run.get('authored_trace_fixture',False)),scope='offline Shared packet identity and coverage, same binary hash required',gpu_library_sha256=artifact['sha256'],traces=list(trace_quality.values()),probe_groups=[],slow_packets=[],long_frame_coverage=[])
 for(key,values)in statuses.items():
  out['probe_groups'].append(dict(pid=key[0],tid=key[1],reports=len(values),first=values[0],last=values[-1],max_report_gap_ms=max((b['mono_ns']-a['mono_ns'])/1e6 for a,b in zip(values,values[1:]))if len(values)>1 else None))
 names={0:'draw',1:'ordered-task',2:'dispatch',3:'special',4:'indirect',5:'indirect-dispatch'}
 for r in packets:
  key=(r['pid'],r['tid'],r['start'],r['wall']);same=lambda x:(x['pid'],x['tid'],x['start'],x['wall'])==key
  ids=[x for x in identity if same(x)];target=[x for x in targets if same(x)];ws=[x for x in windows if x['pid']==r['pid']and x['tid']==r['tid']and x['start']<=r['start']and abs(x['end']-r['end'])<=1000000]
  item=dict(pid=r['pid'],tid=r['tid'],start_mono_ns=r['start'],packet_wall_ms=r['wall']/1e6,payload_bytes=r['arg'],kind=None)
  if len(ids)==1:
   encoded=ids[0]['arg']>>32;kind=encoded-1 if encoded else None;item.update(kind=names.get(kind,'unknown'),kind_number=kind,identity_bytes=ids[0]['arg']&0xffffffff)
  if len(ws)==1:item['cpu_bounds_ms']=dict(lower=max(0,ws[0]['cpu']-ws[0]['arg'])/1e6,upper=ws[0]['cpu']/1e6,slack=ws[0]['arg']/1e6)
  if len(target)==1:
   offset=target[0]['arg'];address=preferred+offset;item['task_image_offset']=offset
   if preferred<=address<preferred+size:
    at=bisect.bisect_right(addresses,address)-1
    if at>=0:
     symbol_address,symbol=symbols[at];pretty=subprocess.check_output(['/usr/bin/c++filt'],input=symbol+'\n',text=True).strip()
     item['symbol']=dict(raw=symbol,demangled=pretty,address=symbol_address,delta=address-symbol_address,exact_entry=address==symbol_address)
   else:item['symbol_error']='task offset outside current __TEXT segment'
  out['slow_packets'].append(item)
 frames=[r for r in rows if r['kind']==40 and r['wall']>=40000000]
 for frame in sorted(frames,key=lambda x:x['wall'],reverse=True)[:12]:
  item=dict(frame_ms=frame['wall']/1e6,start_mono_ns=frame['start'],end_mono_ns=frame['end'],groups=[])
  for(key,values)in statuses.items():
   if key[0]!=frame['pid']:continue
   before=[x for x in values if x['mono_ns']<=frame['start']];after=[x for x in values if x['mono_ns']>=frame['end']]
   if not before or not after:continue
   a,b=before[-1],after[0];saturated=any(x[k]==limit for x in [a,b]for k,limit in [('anchors',0xffffffff),('slow',0xffff),('rejected',0xffff)])
   monotonic=all(b[k]>=a[k]for k in ['anchors','slow','rejected']);near=b['mono_ns']-a['mono_ns']<=3000000000
   captured=sum(r['pid']==key[0]and r['tid']==key[1]and a['seq']<r['seq']<b['seq']for r in packets)
   slow_delta=b['slow']-a['slow'];reject_delta=b['rejected']-a['rejected']
   item['groups'].append(dict(pid=key[0],tid=key[1],before=a,after=b,slow_delta=slow_delta,rejected_delta=reject_delta,captured_slow_in_publication_bracket=captured,saturated=saturated,nearby_reports=near,no_rejected_samples_in_bracket=bool(trace_quality[key[0]]['all_published']and near and monotonic and not saturated and reject_delta==0 and captured==slow_delta),warning='Counter bracket includes up to about one second around frame; proves monitored packet coverage in bracket, not thread scheduling cause or whole-game completeness'))
  out['long_frame_coverage'].append(item)
 out['warning']='Task offset exact-entry resolves identity, not a stack trace or causal proof. CPU bounds include extra sample window; do not sum nested/overlapping events. No shutdown/tail coverage assumed beyond last periodic report.'
 (e/'packet-identity-summary.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({k:v for k,v in out.items()if k not in ['long_frame_coverage','warning']},indent=2));print('long frame coverage',len(out['long_frame_coverage']))
if __name__=='__main__':main()
