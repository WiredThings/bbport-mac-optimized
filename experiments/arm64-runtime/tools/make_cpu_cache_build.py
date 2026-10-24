#!/usr/bin/env python3
"""Bind the shared compiled translator/bridge into both CPU-cache executables."""
from pathlib import Path
import argparse,hashlib,json,os,struct
P=Path(__file__).resolve().parents[1]
def digest_file(path):
 path=path.resolve(strict=True);assert path.is_relative_to(P) and path.is_file() and not path.is_symlink()
 before=path.stat();assert 0<before.st_size<=64*1024*1024
 with path.open('rb')as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
 after=path.stat();assert(before.st_ino,before.st_size,before.st_mtime_ns)==(after.st_ino,after.st_size,after.st_mtime_ns)
 return digest

def combine(domain,rows):
 h=hashlib.sha256();h.update(domain.encode()+b'\0')
 for label,digest in sorted(rows.items()):
  raw=label.encode();h.update(struct.pack('<Q',len(raw)));h.update(raw);h.update(bytes.fromhex(digest))
 return h.hexdigest()

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--runtime-part',action='append',default=[]);ap.add_argument('--bridge-part',action='append',default=[]);ap.add_argument('--header',type=Path,required=True);ap.add_argument('--manifest',type=Path,required=True);a=ap.parse_args()
 def parts(args):
  out={}
  for row in args:
   label,path=row.split('=',1);assert label and label.isascii() and label not in out
   out[label]=digest_file(Path(path))
  assert out;out['builder-source']=digest_file(Path(__file__));return out
 runtime,bridge=parts(a.runtime_part),parts(a.bridge_part)
 report=dict(schema=1,runtime_parts=runtime,bridge_parts=bridge,runtime_build=combine('BE-common-runtime-build-v1',runtime),bridge_build=combine('BE-common-bridge-build-v1',bridge))
 def literal(hex):return ','.join('0x'+hex[i:i+2]for i in range(0,len(hex),2))
 header='// Generated from actual linked archives and frontend/bridge inputs; do not edit.\n#pragma once\n#include "bb_cpu_cache_identity.h"\nnamespace BbCacheBuild {\n'
 header+='inline constexpr BbCpuCache::Digest Runtime={'+literal(report['runtime_build'])+'};\n'
 header+='inline constexpr BbCpuCache::Digest Bridge={'+literal(report['bridge_build'])+'};\n}\n'
 for path,text in [(a.header,header),(a.manifest,json.dumps(report,indent=2)+'\n')]:
  path=path.absolute();assert path.parent.resolve().is_relative_to(P)
  if path.exists()and path.read_text()==text:continue
  temp=path.with_suffix(path.suffix+'.new');temp.write_text(text);os.replace(temp,path)
 print('CPU cache common build identity:',report['runtime_build'])
if __name__=='__main__':main()
