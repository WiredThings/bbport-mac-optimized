#!/usr/bin/env python3
from pathlib import Path
import sys,struct,hashlib,copy
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from cpu_code_map import parse,InvalidCodeMap
identity=hashlib.sha256(b'authored map identity').hexdigest();fid=int.from_bytes(bytes.fromhex(identity)[:8],'little');name='/fixture/authored.bin'
m=dict(identity_sha256=identity,file_id=f'{fid:x}',source=name,image_bytes=16384,eligible_bytes=8192,sections=[[4096,8192],[12288,16384]])
marker=struct.pack('<QI',2**64-1,2**32-1)+struct.pack('<Q',fid)+name.encode()+b'\0';marker+=b'\0'*((-len(marker))%4)
row=lambda offset:struct.pack('<QI',fid,offset)
valid=marker+row(4096)+row(8191)+row(12288)+row(4096);checked=0
def check(value):
 global checked;checked+=1
 if not value:raise AssertionError(checked)
r=parse(valid,m);check(r['records']==4 and r['unique_blocks']==3)
ordered=parse(marker+row(12288)+row(4096)+row(8191)+row(12288),m,preserve_order=True)
check(ordered['observed_unique_order']==[12288,4096,8191])
check(ordered['blocks']==[4096,8191,12288])
check('observed_unique_order'not in r)
# Concurrent first-library publication can race an entry; final parsing validates against the full table.
check(parse(row(4096)+marker,m)['unique_blocks']==1)
def reject(data,manifest=m):
 try:parse(data,manifest)
 except InvalidCodeMap:return True
 return False
for data in [b'',valid[:-1],marker+row(0),marker+row(8192),marker+row(16384),marker+struct.pack('<QI',fid^1,4096),marker+marker+row(4096),row(4096),marker[:-1],marker+struct.pack('<QI',2**64-1,100),marker+struct.pack('<QI',2**64-1,0xffffffee)+struct.pack('<Q',fid)+row(4096)]:check(reject(data))
for field,value in [('file_id','1'),('identity_sha256','bad'),('eligible_bytes',1),('sections',[[4096,8192],[8191,12288]]),('sections',[[False,8192]]),('image_bytes',True),('source','relative')]:
 bad=copy.deepcopy(m);bad[field]=value;check(reject(valid,bad))
print(f'CPU_CODE_MAP checks={checked} passed=1')
