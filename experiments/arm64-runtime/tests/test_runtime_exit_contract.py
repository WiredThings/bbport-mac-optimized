"""Real authored x86 exits plus the unchanged launcher supervisor; no game."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,time
P=Path(__file__).resolve().parents[1]
if not (P/'src/runtime_exit_codes.h').exists():
    P=Path('/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype')
sys.path.insert(0,str(P/'tests'))
from game_fixture import make
E=P/'evidence'/('exit-contract-'+str(time.time_ns()));E.mkdir()
scratch=P/'state'/('exit-fixtures-'+str(time.time_ns()));scratch.mkdir(mode=0o700)
gpu=scratch/'gpu';gpu.mkdir();config=scratch/'bbport.ini';config.write_text('')
env=dict(PATH='/usr/bin:/bin:/usr/sbin:/sbin',HOME=str(P/'state'),TMPDIR=str(scratch)+'/',BB_GPU_USER_DIR=str(gpu),BB_CONFIG=str(config),BB_ONLINE='0',BB_QUICKSAVE='0',BB_AUDIO='none')
results=[]
def run_fixture(mode,expected,needle=None):
    fixture=P/'fixtures'/('exit-'+str(time.time_ns())+'.bbprobe');fixture.write_bytes(make(mode))
    state=P/'state'/('exit-runtime-'+str(time.time_ns()))
    r=subprocess.run([str(P/'build/fex/Bin/bb-game-arm64'),'--authored-fixture','--boot',str(fixture),'--state',str(state)],env=env,capture_output=True,text=True,timeout=30)
    (E/(mode.replace(':','-')+'.log')).write_text(r.stdout+r.stderr)
    assert r.returncode==expected,(mode,r.returncode,r.stderr[-1500:])
    if needle:assert needle in r.stderr,(mode,r.stderr[-1500:])
    results.append(dict(mode=mode,status=r.returncode))
run_fixture('missing-import',80,'ARM UNIMPLEMENTED: missing:')
for operation in ['guest-exit','guest-hard-exit']:
    for raw in [0,1,75,76,331,332,-181,-180]:
        expected=82 if raw&255 in (75,76) else raw&255
        run_fixture(operation+':'+str(raw),expected)
original=P/'launcher-component-stage/tools/macos/supervise_restart.sh'
supervisor=[]
for statuses,expected,count in [([80],80,1),([81],81,1),([82],82,1),([83],83,1),([75,0],0,2),([76,75,0],0,3)]:
    root=scratch/('supervisor-'+str(statuses[0]));(root/'tools/macos').mkdir(parents=True)
    shutil.copy2(original,root/'tools/macos/supervise_restart.sh')
    (root/'count').write_text('0')
    body='#!/bin/bash\nn=$(cat count)\nn=$((n+1))\nprintf "%s" "$n" > count\nprintf "%s %s %s %s\\n" "$n" "${BB_ONLINE-unset}" "${BB_QUICKLOAD_PENDING-unset}" "${BB_THD_TOKEN-unset}" >> env-observed\n'
    for i,status in enumerate(statuses,1):body+=f'if [[ $n == {i} ]]; then exit {status}; fi\n'
    (root/'run.sh').write_text(body+'exit 99\n')
    q=subprocess.run(['/bin/bash',str(root/'tools/macos/supervise_restart.sh')],env=env|{'BB_ONLINE':'1','BB_THD_TOKEN':'authored-test-only'},capture_output=True,text=True,timeout=10)
    observed=(root/'env-observed').read_text().splitlines()
    assert q.returncode==expected and int((root/'count').read_text())==count,(statuses,q.returncode,observed)
    if statuses[0]==76:
        assert observed==['1 1 unset authored-test-only','2 0 1 unset','3 0 unset unset'],observed
    supervisor.append(dict(statuses=statuses,status=q.returncode,launches=count,environment=observed))
    (E/('supervisor-'+str(statuses[0])+'.log')).write_text(q.stdout+q.stderr)
q=subprocess.run([sys.executable,str(P/'tests/test_launcher_restart.py')],capture_output=True,text=True,timeout=40)
(E/'restart-owned-reap.log').write_text(q.stdout+q.stderr);assert q.returncode==0,q.stdout+q.stderr
result=dict(passed=True,authored_FEX=results,unchanged_supervisor=supervisor,binary_sha256=hashlib.file_digest((P/'build/fex/Bin/bb-game-arm64').open('rb'),'sha256').hexdigest(),game_not_launched=True)
(E/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(E);print(json.dumps(result,indent=2))
