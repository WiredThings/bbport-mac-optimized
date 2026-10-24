"""Actual authored guest calls must validate full libc spans before host entry."""
from pathlib import Path
import hashlib,json,os,subprocess,time
from game_fixture import make
P=Path(__file__).resolve().parents[1]
E=P/'evidence'/('libc-spans-'+str(time.time_ns()));E.mkdir()
scratch=P/'state'/('libc-spans-fixtures-'+str(time.time_ns()));scratch.mkdir(mode=0o700)
(scratch/'gpu').mkdir();(scratch/'bbport.ini').write_text('')
env=dict(PATH='/usr/bin:/bin:/usr/sbin:/sbin',HOME=str(P/'state'),TMPDIR=str(scratch)+'/',BB_GPU_USER_DIR=str(scratch/'gpu'),BB_CONFIG=str(scratch/'bbport.ini'),BB_ONLINE='0',BB_QUICKSAVE='0',BB_AUDIO='none')
cases=[]
for operation in ['memcpy','memmove','memset','memcmp']:
    variants=['valid','destination','huge']
    if operation!='memset':variants+=['source']
    if operation!='memcmp':variants+=['readonly']
    if operation=='memmove':variants+=['overlap']
    for variant in variants:
        name=operation+'-'+variant;fixture=P/'fixtures'/('libc-spans-'+name+'.bbprobe')
        fixture.write_bytes(make('libc-span:'+operation+':'+variant))
        state=P/'state'/('libc-span-runtime-'+str(time.time_ns()))
        q=subprocess.run([str(P/'build/fex/Bin/bb-game-arm64'),'--authored-fixture','--boot',str(fixture),'--state',str(state)],env=env,capture_output=True,text=True,timeout=20)
        expected=0 if variant in ['valid','overlap']else 74
        (E/(name+'.log')).write_text(q.stdout+q.stderr)
        assert q.returncode==expected,(name,q.returncode,q.stderr[-1500:])
        if expected:
            markers=['ARM ABI STOP runtime.guest_'+operation+'.span']
            if variant=='readonly':markers.append('ARM HLE STOP')
            assert any(marker in q.stderr for marker in markers),(name,q.stderr[-1500:])
        cases.append(dict(operation=operation,variant=variant,exit=q.returncode))
result=dict(passed=True,cases=cases,game_not_launched=True,binary_sha256=hashlib.file_digest((P/'build/fex/Bin/bb-game-arm64').open('rb'),'sha256').hexdigest())
(E/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(E);print(json.dumps(result,indent=2))
