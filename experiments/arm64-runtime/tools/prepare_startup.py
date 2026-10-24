#!/usr/bin/env python3
"""Compile the guarded 1.09 startup and variable-delta-time FPS patches, with exact expected-byte guards.
Game inputs are read only; payload and manifest go in the private prototype.
"""
from pathlib import Path
import struct,json,hashlib,xml.etree.ElementTree as ET,importlib.util
P=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('launcher_patch_policy',P/'vendor/blood_echoes/launcher_patches.py')
policy=importlib.util.module_from_spec(spec);spec.loader.exec_module(policy)
def prepare(boot,out,config=None,game=None,*,online=False,language_environment=None):
 assert type(online)is bool
 boot=boot.resolve();assert boot.is_relative_to(P.parent/'assets')
 config=(config or P/'fixtures/correctness.ini').resolve();assert config.parent==(P/'fixtures').resolve()
 settings=policy.read_settings(config)
 env=dict(BB_RENDERER='metal',BB_ONLINE='1'if online else'0',BB_LANGUAGE='11',BB_SKIP_NETWORK_CHOICE='0'if online else'1',BB_METAL_OBJECT_MOTION='0')
 if language_environment is not None:env.update(language_environment)
 names=list(policy.FPS_PRESETS['uncap'])+policy.effect_patches(settings)+policy.language_patches(env)
 if policy.skip_network_choice(env):names.append(policy.SKIP_NETWORK_CHOICE)
 names=list(dict.fromkeys(policy.validate_patch_requirements(names,game or P.parent/'assets/CUSA03173-1.09')))
 sizes=policy.scaled_sizes(settings)
 assert sizes is None, 'resolution-changing profiles must use the shared viewport-patch adapter before testing'

 with boot.open('rb') as f:
  assert f.read(8)==b'BBPROBE5';size=struct.unpack('<Q',f.read(8))[0]
  image=boot.stat().st_size-size;assert image>0
  records=[];details=[]
  xml=P/'vendor/blood_echoes/Bloodborne.xml'
  metas={m.get('Name'):m for m in ET.parse(xml).getroot().iter('Metadata') if m.get('AppVer')=='01.09' and m.get('AppElf','eboot.bin')=='eboot.bin'}
  for name in names:
   m=metas[name];assert m.get('AppVer')=='01.09'
   for offset,after in policy.compile_patches(xml,[name],'01.09',[(0,size)]):
    assert 0<=offset<=size-len(after)
    f.seek(image+offset);before=f.read(len(after));assert len(before)==len(after)
    records.append((offset,before,after));details.append(dict(name=name,offset=hex(offset),before=before.hex(),after=after.hex(),author=m.get('Author')))
 policy.validate_online_writes([(a,c) for a,b,c in records],P/'vendor/blood_echoes/Bloodborne.xml','01.09',[(0,size)],env)
 policy.validate_velocity_writes([(a,c) for a,b,c in records],settings)
 # The upstream XML deliberately revises a few writes later in the same
 # named patch. Preserve its ordered last-write semantics, then flatten to
 # nonoverlapping records so the runtime still checks everything atomically.
 final={};owners={};overrides=[]
 for (offset,before,after),detail in zip(records,details):
  for i,value in enumerate(after):
   at=offset+i
   if at in final and final[at][1]!=value:
    assert owners[at]==detail['name'], 'conflicting independent startup patches'
    overrides.append(dict(offset=hex(at),patch=detail['name'],old=final[at][1],new=value))
   final[at]=(before[i],value);owners[at]=detail['name']
 records=[]
 for at,(old,new) in sorted(final.items()):
  if records and at==records[-1][0]+len(records[-1][1]) and len(records[-1][1])<4096:
   offset,before,after=records[-1];records[-1]=(offset,before+bytes([old]),after+bytes([new]))
  else:records.append((at,bytes([old]),bytes([new])))
 assert 0<len(records)<=512
 blob=b'BBSTART1'+struct.pack('<I',len(records))+b''.join(struct.pack('<QI',a,len(b))+b+c for a,b,c in records)
 with boot.open('rb')as digest_file:boot_digest=hashlib.file_digest(digest_file,'sha256').hexdigest()
 out.write_bytes(blob);out.with_suffix('.json').write_text(json.dumps(dict(boot=str(boot),boot_sha256=boot_digest,config=str(config),config_sha256=hashlib.sha256(config.read_bytes()).hexdigest(),selected_names=names,language=int(env['BB_LANGUAGE']),language_source=env.get('BB_CHINESE_SOURCE','official'),online=online,fps="uncap",vblank_hz=480,normalized_records=len(records),ordered_overrides=overrides,patches=details),indent=2))
 return out
if __name__=='__main__':print(prepare(P.parent/'assets/boot-linked.bin',P/'fixtures/startup.bin'))
