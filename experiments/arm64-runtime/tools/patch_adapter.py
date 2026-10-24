"""Convert the same guarded private startup edits to the original probe's patch wire format."""
import struct
from pathlib import Path
def convert(startup:Path,out:Path):
 raw=startup.read_bytes()
 if len(raw)<12 or raw[:8]!=b'BBSTART1':raise ValueError('invalid guarded header')
 count=struct.unpack_from('<I',raw,8)[0]
 if not 0<count<=512:raise ValueError('invalid guarded count')
 at=12;edits=[];end=0
 for _ in range(count):
  if at+12>len(raw):raise ValueError('short record')
  offset,size=struct.unpack_from('<QI',raw,at);at+=12
  if not 0<size<=4096 or at+2*size>len(raw) or offset<end:raise ValueError('invalid ordered edit')
  before=raw[at:at+size];after=raw[at+size:at+2*size];at+=2*size;end=offset+size
  edits.append((offset,after))
 if at!=len(raw):raise ValueError('trailing guarded bytes')
 out.write_bytes(b'BBPATCH2'+struct.pack('<QQ',0x400000,len(edits))+b''.join(struct.pack('<QQ',off,len(value))+value for off,value in edits))
 return edits
