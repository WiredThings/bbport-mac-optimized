#!/usr/bin/env python3
"""Attribute only stable, fully contained kernel regions to known FEX owners."""
import re,collections
from decimal import Decimal
KINDS={1:'jit-code',2:'thread-lookup',3:'dispatcher'}
EVENT=re.compile(r'^FEX_MAP pid=(\d+) seq=(\d+) mono=(\d+) action=([AF]) kind=(\d+) address=(0x[0-9a-f]+) bytes=(\d+)$',re.M)
REGION=re.compile(r'^(.*?)\s+([0-9a-fA-F]{6,16})-([0-9a-fA-F]{6,16})\s+\[\s*(\d+(?:\.\d+)?)\s+(\d+(?:\.\d+)?)\s+(\d+(?:\.\d+)?)\s+(\d+(?:\.\d+)?)\s*\]\s+(\S+)\s+SM=(\S+)')
def lifecycle(text,pid):
 if re.search(r'^FEX_MAP_(?:INVALID|OVERFLOW) pid='+str(pid)+r'\b',text,re.M):raise ValueError('mapping diagnostic invalid/overflow')
 matches=list(EVENT.finditer(text));events=[]
 for m in matches:
  p,seq,mono,action,kind,address,size=m.groups()
  if int(p)==pid:events.append(dict(seq=int(seq),mono=int(mono),action=action,kind=int(kind),address=int(address,16),size=int(size)))
 candidates=[l for l in text.splitlines()if l.startswith('FEX_MAP ')and re.search(r'\bpid='+str(pid)+r'\b',l)]
 if len(candidates)!=len(events):raise ValueError('malformed mapping event')
 events.sort(key=lambda e:e['seq'])
 if not events or [e['seq']for e in events]!=list(range(1,len(events)+1)):raise ValueError('missing/duplicate mapping sequence')
 active={}
 for e in events:
  if e['kind']not in KINDS or not e['mono']or not 0<e['address']<2**64 or not 0<e['size']<2**64-e['address']:raise ValueError('invalid mapping range')
  key=e['kind'],e['address']
  if e['action']=='A':
   if key in active:raise ValueError('allocation overwrote a live owner')
   if any(e['address']<v['address']+v['size']and v['address']<e['address']+e['size']for v in active.values()):raise ValueError('overlapping live owners')
   active[key]=e
  else:
   if key not in active or active[key]['size']!=e['size']:raise ValueError('unknown/mismatched mapping free')
   del active[key]
 return active,events

def regions(text,pid):
 if not re.search(r'^Process:.*\['+str(pid)+r'\]',text,re.M):raise ValueError('kernel process identity mismatch')
 m=re.search(r'^VM page size:\s+(\d+) bytes$',text,re.M)
 if not m:raise ValueError('missing kernel page units')
 page=int(m[1])
 if not(page>=4096 and page<=65536 and not(page&(page-1))):raise ValueError('invalid kernel page size')
 out=[]
 for line in text.splitlines():
  m=REGION.match(line)
  if not m:
   if re.search(r'[0-9a-fA-F]{6,16}-[0-9a-fA-F]{6,16}\s+\[',line):raise ValueError('unparsed kernel region')
   continue
  name,a,b,v,r,d,s,prot,mode=m.groups();a,b=int(a,16),int(b,16);values=[Decimal(x)*page for x in [v,r,d,s]]
  if a>=b or abs(values[0]-(b-a))>Decimal(page)/100 or any(n>b-a+Decimal(page)/100 for n in values[1:]):raise ValueError('invalid kernel region sizes')
  exact=all(x==x.to_integral_value()for x in values)and values[0]==b-a
  out.append(dict(name=name.strip(),start=a,end=b,virtual_bytes=b-a,resident_bytes=int(values[1])if exact else None,dirty_bytes=int(values[2])if exact else None,swapped_bytes=int(values[3])if exact else None,exact_page_counts=exact,protection=prot,mode=mode))
 if not out:raise ValueError('no kernel regions parsed')
 out.sort(key=lambda r:r['start'])
 if any(a['end']>b['start']for a,b in zip(out,out[1:])):raise ValueError('overlapping kernel regions')
 return out,page

def associate(before,after,kernel,pid,required_after_mono_ns=None):
 a,events_a=lifecycle(before,pid);b,events_b=lifecycle(after,pid)
 checkpoint=None
 if required_after_mono_ns is not None:
  checkpoints=[dict(mono=int(t),issued=int(n),failed=int(f))for p,t,n,f in re.findall(r'^FEX_MAP_CHECKPOINT pid=(\d+) mono=(\d+) issued=(\d+) failed=(\d+)$',after,re.M)if int(p)==pid and int(t)>=required_after_mono_ns]
  if not checkpoints:raise ValueError('no fresh post-kernel publication checkpoint')
  checkpoint=max(checkpoints,key=lambda x:x['mono'])
  if checkpoint['failed']or checkpoint['issued']>len(events_b):raise ValueError('mapping output incomplete or failed')
 if events_b[:len(events_a)]!=events_a:raise ValueError('mapping history changed/truncated')
 stable=[v for k,v in a.items()if k in b and b[k]['seq']==v['seq']];changed=len(a)-len(stable);maps,page=regions(kernel,pid);groups={k:dict(allocations=sum(v['kind']==k for v in stable),virtual_bytes=0,resident_bytes=0,dirty_bytes=0,swapped_bytes=0,regions=0)for k in KINDS};unknown=dict(regions=0,virtual_bytes=0,resident_bytes=0,dirty_bytes=0,swapped_bytes=0,unquantified_regions=0);partial=0;used=set()
 for r in maps:
  hit=[v for v in stable if v['address']<=r['start']and r['end']<=v['address']+v['size']]
  if len(hit)>1:raise ValueError('ambiguous owner')
  group=unknown
  if hit and r['exact_page_counts']:group=groups[hit[0]['kind']];used.add(hit[0]['seq'])
  elif any(r['start']<v['address']+v['size']and v['address']<r['end']for v in stable):partial+=1
  group['regions']+=1
  if not r['exact_page_counts']:unknown['unquantified_regions']+=1
  for name in ['virtual_bytes','resident_bytes','dirty_bytes','swapped_bytes']:group[name]+=r[name]if r[name]is not None else 0
 return dict(publication_checkpoint=checkpoint,pid=pid,page_size=page,events_before=len(events_a),events_after=len(events_b),stable_allocations=len(stable),allocations_changed_during_snapshot=changed,stable_allocations_with_no_complete_region=len(stable)-len(used),partial_regions_unattributed=partial,categories={KINDS[k]:v for k,v in groups.items()},unattributed=unknown,scope='kernel region bytes only, stable allocation generations and complete containment; virtual != resident, these per-map counters are not footprint totals or de-duplicated process RAM',not_covered=['rounded page counters remain unquantified and unattributed','IR/PMR/HLE veneers and other allocators not instrumented','subregion L1/L2 residency and temporary owner changes','whether any still-live reference can safely be released'])
