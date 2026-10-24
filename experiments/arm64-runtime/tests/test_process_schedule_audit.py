"""SDK layout, live identity, counter units and rejected invalid PIDs; no game."""
from pathlib import Path
import ctypes as C,json,os,subprocess,sys,tempfile,time
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'tools'))
from process_schedule_audit import Usage4,FIELDS,ProcessScheduleAudit
source='''#include <sys/resource.h>\n#include <stddef.h>\n#include <stdio.h>\nint main(){printf("%zu",sizeof(struct rusage_info_v4));'''+''.join('printf(" %zu",offsetof(struct rusage_info_v4,ri_'+n+'));'for n in ['user_time','cpu_time_qos_default','instructions','runnable_time'])+'puts("");}'
with tempfile.TemporaryDirectory(prefix='bb-rusage4-sdk-')as d:
 p=Path(d);(p/'test.c').write_text(source);subprocess.run(['/usr/bin/clang',str(p/'test.c'),'-o',str(p/'test')],check=True)
 layout=list(map(int,subprocess.check_output([str(p/'test')],text=True).split()))
 assert layout==[C.sizeof(Usage4)]+[getattr(Usage4,n).offset for n in ['user_time','cpu_time_qos_default','instructions','runnable_time']],layout
observer=ProcessScheduleAudit()
for pid in [0,-1,1<<32,True]:
 try:observer.read(pid)
 except ValueError:pass
 else:raise AssertionError('invalid PID accepted')
a=observer.read(os.getpid());start=time.process_time_ns();stop=time.monotonic()+.25
while time.monotonic()<stop:pass
elapsed=time.process_time_ns()-start;b=observer.read(os.getpid())
assert a['uuid']==b['uuid']and a['process_start_mach']==b['process_start_mach']
assert b['counters']['user_time']+b['counters']['system_time']>a['counters']['user_time']+a['counters']['system_time']
assert b['counters']['runnable_time']>=a['counters']['runnable_time']
print(json.dumps(dict(passed=True,SDK_layout=layout,process_cpu_ns=elapsed,cpu_raw_delta=sum(b['counters'][n]-a['counters'][n]for n in ['user_time','system_time']),runnable_raw_delta=b['counters']['runnable_time']-a['counters']['runnable_time'],qos_raw_delta=sum(b['counters'][n]-a['counters'][n]for n in FIELDS if n.startswith('cpu_time_qos_')),game_started=False)))
