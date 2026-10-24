"""Original production supervisor protocol with mocked owned GPU reap; no game files."""
from pathlib import Path
import subprocess
P=Path(__file__).resolve().parents[1]
exe=P/'build/fex/Bin/test_native_restart'
env={'PATH':'/usr/bin:/bin','BB_USER_DIR':str(P/'state/authored-launcher-restart'),'BB_ARM_LAUNCHER_COMPAT':'1','BB_RESTART_SUPERVISED':'1','BB_ARM_RESTART_NONCE':'a'*32}
for load,status in [(None,75),('1',76)]:
 trial=dict(env)
 if load:trial['BB_QUICKLOAD_PENDING']=load
 r=subprocess.run([exe],env=trial,capture_output=True,text=True,timeout=10)
 assert r.returncode==status and'backend reaped'in r.stdout,(r.returncode,r.stdout,r.stderr)
for key,value in [('BB_RESTART_SUPERVISED','0'),('BB_ARM_RESTART_NONCE','bad'),('BB_USER_DIR','/private/tmp/not-private')]:
 r=subprocess.run([exe],env=env|{key:value},capture_output=True,text=True,timeout=10);assert r.returncode==78
print('PASS original supervisor statuses 75/76 after mocked owned-GPU reap; reject missing supervisor/forged nonce/nonprivate user')
