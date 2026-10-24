#!/usr/bin/env python3
"""Authored x86 bodies: exercise all ten startup resource detours under FEX.
No game code or assets. Inline observers deliberately clobber volatile GPR/XMM.
"""
from pathlib import Path
import struct,re
P=Path(__file__).resolve().parents[1]
def make(bad_metadata=False,corrupt=False):
 data=bytearray(32768);names=[];relocs=[];fix=[]
 nids={symbol:nid for nid,symbol in re.findall(r'\{"([^"]+)\",\"([^\"]+)\"\}',(P/'vendor/bb_runtime/src/import_names.inc').read_text())}
 nids['bb.prototype.resource_check']='bb.prototype.resource_check'
 nids['bb.prototype.resource_bad_check']='bb.prototype.resource_bad_check'
 def store(at,code):data[at:at+len(code)]=code
 # Actual function entry prologues, not native calls. Each original body is
 # reached through the newly emitted guest trampoline by nested FEX CallGuest.
 store(0x2000,bytes.fromhex('55 48 89 e5 41 57 41 56 ff 47 28 41 5e 41 5f 5d c3'))
 store(0x2100,bytes.fromhex('53 31 c0 48 39 77 18 48 8b 07 ff 47 20 5b c3'))
 small=bytes.fromhex('41 57 41 56 41 55 41 54 53 48 83 fa 10 77 0e 5b 41 5c 41 5d 41 5e 41 5f e9')+struct.pack('<i',0x2100-0x2200-29)
 store(0x2200,small+bytes.fromhex('48 8b 07 5b 41 5c 41 5d 41 5e 41 5f c3'))
 for site in [0x1800,0x1900]:
  at=site-0x40;store(at,bytes.fromhex('48 83 ec 08 e9')+struct.pack('<i',site-at-9))
  store(site,b'\xe8'+struct.pack('<i',0x2600-site-5)+bytes.fromhex('48 83 c4 08 c3'))
 store(0x2600,bytes.fromhex('48 89 f8 48 89 d1 f3 a4 c3'))
 store(0x19c0,bytes.fromhex('53 48 83 ec 18 48 89 f8 e9')+struct.pack('<i',0x1a00-0x19c0-13))
 store(0x1a00,bytes.fromhex('48 83 c4 18 5b c3'))
 for site in [0x1b00,0x1c00,0x1d00]:
  at=site-0x40;pre=bytes.fromhex('55 48 89 e5 53 48 81 ec 88 00 00 00 48 89 75 b0 48 89 f8 e9');store(at,pre+struct.pack('<i',site-at-len(pre)-4))
  store(site,bytes.fromhex('48 89 c3 48 85 db 48 89 d8 48 8b 5d f8 c9 c3'))
 store(0x1dc0,bytes.fromhex('41 56 48 83 ec 30 49 89 fe 48 89 74 24 28 e9')+struct.pack('<i',0x1e00-0x1dc0-19))
 store(0x1e00,bytes.fromhex('48 8b 4c 24 28 48 ff 89 c8 00 00 00 48 83 c4 30 41 5e c3'))
 if corrupt:data[0x2207]^=1
 code=bytearray.fromhex('48 83 ec 08');guards=[]
 def emit(h):code.extend(bytes.fromhex(h))
 def ref(h,target):emit(h);fix.append((0x400+len(code),target));code.extend(bytes(4))
 def call(name):
  if name not in names:names.append(name)
  ref('ff 15',0x4800+8*names.index(name))
 def guard():emit('0f 85');guards.append(len(code));code.extend(bytes(4))
 def eqzero():emit('85 c0');guard()
 def lea(reg,at):ref('48 8d '+reg,at)
 # GPU-eligible memory, one address reused by both heap entry variants.
 call('sceKernelGetDirectMemorySize');emit('48 89 c6 31 ff ba 00 40 00 00 b9 00 40 00 00 45 31 c0');ref('4c 8d 0d',0x4388);call('sceKernelAllocateDirectMemory');eqzero()
 lea('3d',0x4380);emit('be 00 40 00 00 ba 33 00 00 00 31 c9');ref('4c 8b 05',0x4388);emit('41 b9 00 40 00 00');call('sceKernelMapDirectMemory');eqzero()
 ref('48 8b 05',0x4380);ref('48 89 05',0x4400);emit('48 05 00 02 00 00');ref('48 89 05',0x4508);emit('48 05 00 02 00 00');ref('48 89 05',0x4548)
 lea('05',0x4540);ref('48 89 05',0x4518)
 # Range allocator return: clobbered GPR/vector states must be restored by stub.
 ref('c5 7e 6f 3d',0x4600);emit('49 ba 78 56 34 12 00 00 00 00');lea('3d',0x4500);ref('e8',0x19c0)
 lea('15',0x4500);emit('48 39 d0');guard();emit('49 81 fa 78 56 34 12');guard()
 ref('c5 7e 7f 3d',0x4640)
 for at in range(0x4640,0x4660,8):ref('48 83 3d',at);emit('ff');guard();fix[-1]=(fix[-1][0],at-1) # RIP after imm8
 # Heap allocation, free, same-address reuse via aligned entry and another free.
 lea('3d',0x4400);emit('be 80 00 00 00');ref('e8',0x2100);ref('48 3b 05',0x4380);guard()
 lea('3d',0x4400);ref('48 8b 35',0x4380);ref('e8',0x2000)
 lea('3d',0x4400);emit('be 80 00 00 00 ba 40 00 00 00');ref('e8',0x2200);ref('48 3b 05',0x4380);guard()
 lea('3d',0x4400);ref('48 8b 35',0x4380);ref('e8',0x2000)
 # A small-alignment request must still reach the hooked plain allocator.
 lea('3d',0x4400);emit('be 80 00 00 00 ba 10 00 00 00');ref('e8',0x2200);ref('48 3b 05',0x4380);guard()
 lea('3d',0x4400);ref('48 8b 35',0x4380);ref('e8',0x2000)
 # Four unsigned band boundaries: bypass only below the arena and at/above
 # its upper end. Fake free never dereferences the test pointer; increments
 # the original-body counter, so skipped original execution is detected.
 for address in [0x100,0x7000000000,0xefffffffff,0xf000000000]:
  lea('3d',0x4400);emit('48 be');code.extend(struct.pack('<Q',address));ref('e8',0x2000)
 # A CPU-only leaf return must still execute once, with no host callback
 # in leaf mode. A deliberately inconsistent fake allocator returning a GPU
 # address must NOT bypass observation: post-return address guard catches it.
 lea('05',0x4a00);ref('48 89 05',0x4700);ref('48 8b 05',0x4380);ref('48 89 05',0x4740)
 for root,target in [(0x4700,0x2100),(0x4700,0x2200),(0x4740,0x2100)]:
  lea('3d',root);emit('be 80 00 00 00 ba 10 00 00 00');ref('e8',target)
  ref('48 3b 05',root);guard();ref('48 8b 35',root);lea('3d',root);ref('e8',0x2000)
 for at in [0x1ac0,0x1bc0,0x1cc0]:ref('48 8b 3d',0x4380);emit('be 80 00 00 00');ref('e8',at);ref('48 3b 05',0x4380);guard()
 # Both loader copies execute native checked copy; original x86 memcpy never
 # executes. Source, destination and comparison span are entirely authored.
 for at in [0x17c0,0x18c0]:
  ref('48 8b 3d',0x4380);lea('35',0x4600);emit('ba 20 00 00 00');ref('e8',at);ref('48 3b 05',0x4380);guard()
 ref('48 8b 05',0x4380);emit('48 83 38 ff');guard()
 lea('3d',0x4500);lea('35',0x4400);ref('e8',0x1dc0)
 if bad_metadata:
  # Second range callback sees an invalid node; it must disable coverage
  # before any native dereference, leaving execution alive for the check.
  emit('bf 34 12 00 00');ref('e8',0x19c0)
 call('bb.prototype.resource_bad_check' if bad_metadata else 'bb.prototype.resource_check');eqzero()
 ref('48 8b 3d',0x4380);emit('be 00 40 00 00');call('sceKernelMunmap');eqzero()
 ref('48 8b 3d',0x4388);emit('be 00 40 00 00');call('sceKernelReleaseDirectMemory');eqzero()
 emit('31 c0 48 83 c4 08 c3');bad=len(code);emit('b8 51 00 00 00 48 83 c4 08 c3')
 for g in guards:struct.pack_into('<i',code,g,bad-g-4)
 assert len(code)<0xc00;store(0x400,code);store(0,b'\x31\xc0\xc3');data[0x4600:0x4620]=bytes([255])*32
 for root in [0x4400,0x4700,0x4740]:struct.pack_into('<Q',data,root+0x18,0x10000)
 for patch,target in fix:struct.pack_into('<i',data,patch,target-patch-4)
 for i,name in enumerate(names):relocs.append((0x4800+8*i,1,i,0))
 header=b'BBPROBE5'+struct.pack('<6Q',len(data),0x400,2,len(relocs),len(names),1)
 header+=struct.pack('<6Q',0x4000,0x4100,0,16,16,1)
 header+=struct.pack('<7Q',0,512,0,0,0,0,0)+struct.pack('<Q',0)
 header+=struct.pack('<6Q',0,16384,5,16384,16384,6)
 header+=b''.join(nids[n].encode().ljust(128,b'\0') for n in names)
 return header+b''.join(struct.pack('<4Q',*r) for r in relocs)+data
if __name__=='__main__':
 for label,kw in [('resource-hooks',{}),('resource-hooks-bad-metadata',{'bad_metadata':True}),('resource-hooks-corrupt',{'corrupt':True})]:
  (P/'fixtures'/f'{label}.bbprobe').write_bytes(make(**kw))
 print('Authored resource fixtures generated')
