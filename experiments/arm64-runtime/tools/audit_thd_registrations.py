#!/usr/bin/env python3
"""Compiler-preprocessed active THD entry inventory. Does not compile/link/execute."""
from pathlib import Path
import json,re,shlex,subprocess,struct,sys
P=Path(__file__).resolve().parents[1]
def imports():
 with (P.parent/'assets/boot-linked.bin').open('rb')as f:
  def word():return struct.unpack('<Q',f.read(8))[0]
  assert f.read(8)==b'BBPROBE5';size,entry,ns,nr,ni,caps=[word()for _ in range(6)];word();[word()for _ in range(4)];nm=word();f.seek(nm*56,1);nb=word();f.seek(nb*24+ns*24,1);return [f.read(128).split(b'\0',1)[0].decode()for _ in range(ni)]
def main():
 e=Path(sys.argv[1]).resolve();assert e.parent==P/'evidence'and e.name.startswith('x86-boundary-audit-')
 build=P/'build/bbnet-arm64';commands=subprocess.check_output(['/opt/homebrew/bin/ninja','-C',str(build),'-t','commands','bbnet'],text=True).splitlines()
 wanted=['thd/thd_net.cpp','thd/thd_np.cpp','thd/thd_matching2.cpp','core/libraries/network/http.cpp','core/libraries/network/http2.cpp','core/libraries/network/ssl2.cpp']
 rows=[];prototypes={};all_imports={x.split('#')[0]for x in imports()}
 for suffix in wanted:
  command=next(shlex.split(x)for x in commands if x.startswith('/usr/bin/c++ ')and x.endswith('/'+suffix));clean=[];skip=False
  for x in command:
   if skip:skip=False;continue
   if x in ('-o','-MF','-MT','-MQ'):skip=True;continue
   if x in ('-c','-MD','-MMD','-MP'):continue
   clean.append(x)
  # Use exactly the configured include paths; missing build inputs must fail.
  clean[1:1]=['-E','-P']
  result=subprocess.run(clean,cwd=build,capture_output=True,text=True,timeout=60)
  if result.returncode:raise RuntimeError('preprocessing failed for '+suffix+': '+result.stderr[:1000])
  text=result.stdout
  for match in re.finditer(r'AddSymbol\(\s*"([^\"]+)"\s*,\s*"([^\"]+)"\s*,\s*"([^\"]+)"[^;]*?GuestEntry<&([\w:]+)>::Call',text):rows.append(dict(nid=match[1],library=match[2],module=match[3],function=match[4],source=suffix,actual_import=match[1]in all_imports))
  source=Path(command[-1]).read_text(errors='replace')
  for match in re.finditer(r'\bPS4_SYSV_ABI\s+(\w+)\s*\((.*?)\)\s*\{',source,re.S):prototypes[match[1]]=dict(source=suffix,line=source[:match.start()].count('\n')+1,arguments=' '.join(match[2].split()))
  print(suffix,'preprocessed',len(text),'bytes',flush=True)
 for row in rows:row['prototype']=prototypes.get(row['function'])
 (e/'thd-active-registration.json').write_text(json.dumps(dict(scope='six ARM64 compiler-preprocessed files selected by current THD chain; configured includes only, no object/link/game/callback/account operations',audit_only_added_include=None,build_configuration_gap=False,rows=rows,prototypes=prototypes,all_guest_imports=sorted(all_imports)),indent=2)+'\n')
 print('active_registrations',len(rows),'matched_game_imports',sum(x['actual_import']for x in rows))
 for row in rows:
  if row['nid']in('PfSTDCgNMgc','CQtPRSF6Ds8'):print(row)
if __name__=='__main__':main()
