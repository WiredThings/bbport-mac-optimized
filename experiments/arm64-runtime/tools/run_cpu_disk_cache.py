#!/usr/bin/env python3
"""Self-authored FEX cache cross-process experiment. No game assets/windows/data."""
from pathlib import Path
import hashlib,json,os,struct,subprocess,time
P=Path(__file__).resolve().parents[1]
B=P/'build/fex/Bin/test_cpu_disk_cache'
def sha(p):
 with p.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def main():
 e=P/'evidence'/f'cpu-disk-cache-{time.time_ns()}';e.mkdir()
 protected=[P/'build/fex/Bin/bb-game-arm64',P/'backend/bbport/bin/gpu-metal-arm64/libbbgpu.dylib']
 before={str(p):sha(p)for p in protected}
 env=dict(PATH='/usr/bin:/bin:/usr/sbin:/sbin',HOME=str(P/'state'),TMPDIR=str(P/'state')+'/',BB_AUDIO='none')
 result=dict(scope='authored x86 guest only; eager load and explicit host-page finalization; no FPS or game-memory conclusions',binary_sha256=sha(B),rows=[],game_artifacts_unchanged=None)
 def persist(): (e/'result.json').write_text(json.dumps(result,indent=2))
 def run(name,mode,file,expected=0,marker=None,digest=None,base='0x9000000000'):
  assert sha(B)==result['binary_sha256'],'fixture binary changed during experiment'
  started=time.monotonic()
  with(e/(name+'.log')).open('w')as log:
   r=subprocess.run([str(B),mode,str(file),base,digest or(sha(file)if file.exists()else'none')],stdout=log,stderr=subprocess.STDOUT,cwd=P,env=env,timeout=15)
  text=(e/(name+'.log')).read_text();row=dict(name=name,exit=r.returncode,expected=expected,seconds=time.monotonic()-started)
  result['rows'].append(row);persist();print(name,r.returncode,flush=True)
  if r.returncode!=expected or(marker and marker not in text):raise RuntimeError(f'{name}: {text}')
  return text
 try:
  for family in ['narrow','wide']:
   temporary=e/(family+'.fxcc.new');cache=e/(family+'.fxcc')
   run(family+'-generate','generate'+('-wide'if family=='wide'else''),temporary,marker='generated=1',base='0x8000000000')
   run(family+'-validate','validate',temporary,marker='validated=1')
   # Only publish the artifact after the generator exit, fsync and independent structural check succeed.
   temporary.rename(cache)
   text=run(family+'-fresh-process-relocated','load'+('-wide'if family=='wide'else''),cache,marker='changed_guest_recompiled=1')
   assert 'no_fallback_jit=1'in text and'unregistered=1'in text and'stale_entry_after_clear=0'in text
  wide=e/'wide.fxcc'
  run('raw-4k-selector-incomplete-host-page','load-wide-page-probe',wide,22,'complete_host_range=0')
  text=run('host-page-closure-load','load-wide-grouped',wide,marker='changed_guest_recompiled=1')
  assert'no_fallback_jit=1'in text
  data=wide.read_bytes();header=struct.unpack_from('<4sI20s5IQ',data)
  blocks,pages,code_bytes,relocs=header[3:7];at=56
  block_rows=[]
  for _ in range(blocks):
   guest,host,n=struct.unpack_from('<QQQ',data,at);block_rows.append(at);at+=24+n*8
  reloc_start=at;code_start=(at+relocs*48+4095)&~4095;page_start=code_start+code_bytes
  kinds={}
  for i in range(relocs):
   kind=struct.unpack_from('<I',data,reloc_start+i*48+8)[0];kinds[str(kind)]=kinds.get(str(kind),0)+1
  result['wide_cache']=dict(bytes=len(data),blocks=blocks,guest_pages=pages,arm_code_bytes=code_bytes,relocations=relocs,relocation_kinds=kinds,sha256=sha(wide))
  mutations={}
  def edit(name,offset,fmt,value):
   value_bytes=bytearray(data);struct.pack_into(fmt,value_bytes,offset,value);mutations[name]=bytes(value_bytes)
  mutations['short-header']=data[:55]
  mutations['truncated-tail']=data[:-1]
  mutations['extra-tail']=data+b'X'
  edit('wrong-magic',0,'4s',b'BAD!')
  edit('wrong-format',4,'I',999)
  edit('empty-blocks',28,'I',0)
  edit('block-count-overflow',28,'I',0xffffffff)
  edit('guest-pages-overflow',32,'I',0xffffffff)
  edit('code-size-overflow',36,'I',0xffffffff)
  edit('relocation-count-overflow',40,'I',0xffffffff)
  edit('bad-serialized-base',48,'Q',0x1234)
  edit('guest-outside-fixture',block_rows[0],'<Q',0x10000)
  edit('host-outside-cache',block_rows[0]+8,'<Q',code_bytes)
  edit('guest-page-count-overflow',block_rows[0]+16,'<Q',0xffffffffffffffff)
  edit('unknown-relocation',reloc_start+8,'<I',999)
  edit('relocation-outside-code',reloc_start,'<Q',0xffffffffffffffff)
  edit('page-entry-count-overflow',page_start+8,'<Q',0xffffffffffffffff)
  edit('page-entry-not-a-block',page_start+16,'<Q',0x123456)
  for name,bad in mutations.items():
   f=e/(name+'.fxcc');f.write_bytes(bad)
   # Recompute digest deliberately: structural checks must reject even with matching content hash.
   run('reject-'+name,'validate',f,20 if name=='short-header'else 21)
  run('reject-content-hash','validate',wide,21,'rejected_before_FEX_load=1',digest='0'*64)
  other=bytearray(data);other[8]^=0xff;old=e/'different-fex-version.fxcc';old.write_bytes(other)
  run('reject-different-fex-version','load-wide',old,12)
  result['passed']=True
 finally:
  result['game_artifacts_unchanged']=before=={str(p):sha(p)for p in protected}
  result['protected_artifact_sha256']=before;persist()
 print(json.dumps(dict(evidence=str(e),tests=len(result['rows']),passed=result.get('passed',False),game_artifacts_unchanged=result['game_artifacts_unchanged']),indent=2))
 assert result['game_artifacts_unchanged']
 assert sha(B)==result['binary_sha256']
if __name__=='__main__':main()
