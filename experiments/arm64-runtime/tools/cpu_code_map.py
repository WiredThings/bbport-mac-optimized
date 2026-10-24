#!/usr/bin/env python3
"""Strict offset-only FEX code-map reader; unknown/truncated input is never a cache hit."""
import bisect,re,struct
MAX=(1<<64)-1
class InvalidCodeMap(ValueError):pass
def need(ok,message):
 if not ok:raise InvalidCodeMap(message)
def parse(data,manifest, *, preserve_order=False):
 need(isinstance(data,bytes)and len(data)<=64*1024*1024,'map type/limit')
 digest=manifest.get('identity_sha256');source=manifest.get('source');size=manifest.get('image_bytes');sections=manifest.get('sections')
 need(isinstance(digest,str)and re.fullmatch('[0-9a-f]{64}',digest),'identity SHA256')
 need(isinstance(source,str)and source.startswith('/')and'\0'not in source,'source name')
 need(type(size)is int and 0<size<=512*1024*1024,'image size')
 need(isinstance(sections,list)and 0<len(sections)<=131072,'section count')
 spans=[];previous=0
 for row in sections:
  need(isinstance(row,list)and len(row)==2 and all(type(n)is int for n in row),'section type')
  lo,hi=row;need(0<=lo<hi<=size and lo>=previous,'section range/overlap');spans.append((lo,hi));previous=hi
 need(sum(hi-lo for lo,hi in spans)==manifest.get('eligible_bytes'),'eligible count')
 file_id=int.from_bytes(bytes.fromhex(digest)[:8],'little')
 if file_id in(0,MAX):file_id^=0x42454350554d4150
 need(isinstance(manifest.get('file_id'),str)and manifest['file_id']==f'{file_id:x}','file identity hint')
 at=0;definitions={};rows=[];main=None
 while at<len(data):
  need(len(data)-at>=12,'truncated entry');ident,offset=struct.unpack_from('<QI',data,at);at+=12
  if ident==MAX and offset==0xffffffff:
   need(len(data)-at>=8,'truncated library ID');defined=struct.unpack_from('<Q',data,at)[0];at+=8
   end=data.find(b'\0',at,min(len(data),at+4097));need(end>=at,'missing/long library path')
   try:name=data[at:end].decode('utf-8')
   except UnicodeError:raise InvalidCodeMap('invalid source path encoding')
   need(defined==file_id and name==source,'library identity mismatch')
   need(defined not in definitions,'duplicate library declaration');definitions[defined]=name
   padding=(-(end+1))%4;need(end+1+padding<=len(data)and not any(data[end+1:end+1+padding]),'bad source padding');at=end+1+padding
  elif ident==MAX and offset in(0xffffffee,0xffffffef):
   need(len(data)-at>=8,'truncated main ID');main=struct.unpack_from('<Q',data,at)[0];at+=8;need(main==file_id and offset==0xffffffef,'wrong main identity/bitness')
  else:
   need(ident==file_id,'unknown/reserved file ID');rows.append(offset)
 need(file_id in definitions and rows,'missing library or code blocks')
 starts=[lo for lo,hi in spans]
 for offset in rows:
  i=bisect.bisect_right(starts,offset)-1;need(i>=0 and offset<spans[i][1],'entry outside stable executable sections')
 result=dict(file_id=f'{file_id:x}',source=source,identity_sha256=digest,records=len(rows),unique_blocks=len(set(rows)),blocks=sorted(set(rows)),scope='validated observed entries only; a signal-terminated producer may leave an unflushed suffix; no complete coverage or executable-cache claim')
 if preserve_order:result['observed_unique_order']=list(dict.fromkeys(rows))
 return result
