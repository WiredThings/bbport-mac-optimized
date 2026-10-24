#!/usr/bin/env python3
"""Read bounded native texture metadata. Never equate metadata with correct pixels."""
import argparse,json
from pathlib import Path

ALIASES={'gb': 'guest_bytes', 'tg': 'tag_generation', 'ts': 'tag_state', 'co': 'current_owner', 'tc': 'tag_covered', 'ov': 'overlap_valid', 'hd': 'history_dropped', 'ot': 'overlap_truncated', 'ni': 'native_image', 'nv': 'native_view', 'uw': 'unknown_writes', 'cu': 'content_us'}

def analyze(text):
 events=[];invalid=0
 for line in text.splitlines():
  if not line.startswith('NATIVE '):continue
  try:events.append(json.loads(line[7:]))
  except (json.JSONDecodeError,ValueError):invalid+=1
 # References address physical native_lines rows, excluding the prepended frame header.
 physical=[x for x in events if x.get('op')!='frame'];expanded=[]
 for index,event in enumerate(physical):
  if event.get('op')=='texture_binding_batch':
   block=event.get('bindings')
   if not isinstance(block,list)or not 1<=len(block)<=64:invalid+=1;continue
   for binding in block:
    if not isinstance(binding,dict):invalid+=1;continue
    if binding.get('op')=='texture_binding':expanded.append(binding);continue
    record=binding.get('record');element=binding.get('element')
    if type(record)is not int or type(element)is not int or record<0 or record>=index or element<0 or element>=64:
     invalid+=1;continue
    source=physical[record];entries=source.get('bindings')
    if source.get('op')!='texture_binding_batch'or not isinstance(entries,list)or element>=len(entries):invalid+=1;continue
    definition=entries[element]
    if not isinstance(definition,dict)or definition.get('op')!='texture_binding':invalid+=1;continue
    expanded.append(definition)
  elif event.get('op')=='texture_bindings_ref':
   start=event.get('start');count=event.get('count')
   if type(start)is not int or type(count)is not int or start<0 or count<1 or count>64 or start+count>index:
    invalid+=1;continue
   block=physical[start:start+count]
   if any(x.get('op')!='texture_binding'for x in block):invalid+=1;continue
   expanded.extend(block)
  else:expanded.append(event)
 for event in expanded:
  if event.get('op')=='texture_binding':
   for short,full in ALIASES.items():
    if short in event:event[full]=event[short]
 events=[x for x in events if x.get('op')=='frame']+expanded
 headers=[x for x in events if x.get('op')=='frame']
 markers=[x for x in events if x.get('op')=='texture_capture']
 warnings=[];sets={};draws=[];bindings=[]
 if invalid:warnings.append('malformed native JSON')
 if len(headers)!=1:warnings.append('missing or ambiguous frame header')
 if any(x.get('truncated',True)for x in headers):warnings.append('native capture truncated')
 if not markers or any(x.get('schema')!=1 or x.get('keep_set_memo')is not True for x in markers):warnings.append('normal texture-memo path not attested')
 for event in events:
  op=event.get('op')
  if op=='texture_set_incomplete':warnings.append('texture-set observation incomplete')
  if op=='texture_set':
   key=(event['core'],event['stage']);sets[key]={'meta':event,'bindings':{}}
  elif op=='texture_binding':
   bindings.append(event);key=(event['core'],event['stage']);group=sets.get(key)
   if not group or group['meta']['shader']!=event['shader']:warnings.append('binding without matching shader set');continue
   group['bindings'][event['slot']]=event
  elif op=='texture_draw':
   linked=[]
   for stage,shader in ((3,event['vs']),(0,event['ps'])):
    if not shader:continue
    group=sets.get((event['core'],stage))
    if not group or group['meta']['shader']!=shader:
     warnings.append('draw missing current vertex/fragment texture set');continue
    meta=group['meta'];wanted=set(range(meta['slot_base'],meta['slot_base']+meta['count']))
    if wanted!=set(group['bindings']):warnings.append('draw has incomplete texture set')
    linked.extend(group['bindings'].values())
   draws.append({'draw':event,'bindings':linked})
 if not draws:warnings.append('no captured graphics draw')
 # These are review observations only: a released guest allocation may still have valid GPU data.
 review=[{'uid':x.get('uid'),'guest':x.get('guest'),'tag_state':x.get('tag_state'),'current_owner':x.get('current_owner'),'tag_generation':x.get('tag_generation'),'native_view':x.get('native_view')} for x in bindings if not x.get('native_view')or x.get('tag_state')in(2,3)or(x.get('current_owner')and x.get('tag_generation')and x['current_owner']!=x['tag_generation'])]
 return {'metadata_complete':not warnings,'warnings':sorted(set(warnings)),'draw_count':len(draws),'binding_observations':len(bindings),'memo_binding_observations':sum(x.get('source')=='set_memo'for x in bindings),'review_observations':review,'draws':draws,'render_correctness_proven':False,'contents_read':False,'note':'Encoded binding/content counters are not texel content, completion proof, or reclamation authorization; truncation or missing stages is incomplete evidence.'}
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('capture',type=Path);ap.add_argument('--output',type=Path);a=ap.parse_args();r=analyze(a.capture.read_text());v=json.dumps(r,indent=2)+'\n';a.output.write_text(v)if a.output else print(v,end='')
