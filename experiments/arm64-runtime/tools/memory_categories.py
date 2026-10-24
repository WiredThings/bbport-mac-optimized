#!/usr/bin/env python3
"""Strict parser for footprint's byte JSON; these are ledger categories, not RAM sums."""
NUMERIC=('dirty','swapped','clean','reclaimable','wired','regions')
def integer(value):
 if type(value)is not int or not 0<=value<2**64:raise ValueError('invalid nonnegative kernel quantity')
 return value

def parse(data,expected):
 if data.get('unit')!='byte'or type(data.get('bytes per unit'))is not int or data.get('bytes per unit')!=1:raise ValueError('unsupported memory units')
 if data.get('errors'):raise ValueError('footprint reported errors')
 if any(type(pid)is not int or pid<=0 for pid in expected):raise ValueError('invalid expected identity')
 processes=data.get('processes');seen=set();out=[]
 if not isinstance(processes,list):raise ValueError('missing process rows')
 for p in processes:
  pid=integer(p.get('pid'))
  if pid not in expected or pid in seen:raise ValueError('unexpected/duplicate process identity')
  seen.add(pid);pagesize=integer(p.get('page size'))
  if not pagesize or pagesize&(pagesize-1)or type(p.get('translated'))is not bool:raise ValueError('invalid page/architecture metadata')
  cats=p.get('categories')
  if not isinstance(cats,dict)or not cats:raise ValueError('missing categories')
  values={}
  for name,c in cats.items():
   if not isinstance(name,str)or not isinstance(c,dict):raise ValueError('invalid category')
   values[name]={k:integer(c.get(k))for k in NUMERIC}
   if values[name]['swapped']>values[name]['dirty']:raise ValueError('swapped is not a dirty subset')
  out.append(dict(pid=pid,role=expected[pid],name=p.get('name'),translated=p['translated'],page_size=pagesize,footprint_bytes=integer(p.get('footprint')),categories=values))
 if seen!=set(expected):raise ValueError('missing process identity')
 return dict(processes=out,dirty_includes_swapped=True,per_process_categories_exclude_separately_reported_shared_objects=True,deduplicated_footprint_bytes=integer(data.get('total footprint')),tool_total_footprint_bytes=integer(data.get('total footprint')),sum_process_footprint_bytes=sum(p['footprint_bytes']for p in out),warnings=data.get('warnings',[]),shared=data.get('shared',[]),scope='single kernel ledger snapshot: Dirty includes (Swapped); do not add them. Joint total de-duplicates shared objects; per-process totals do not. Footprint includes compressed/swapped charges and is not resident RAM; virtual reservation is not residency')
