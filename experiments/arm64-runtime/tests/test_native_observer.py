#!/usr/bin/env python3
"""Real readback parity, PID identity/exit race, fail-closed negative cases."""
from pathlib import Path
import ctypes as C,json,os,subprocess,sys,time,statistics
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_observer import NativeObserver,SwapUsage
from run_game import system as legacy
p=Path(__file__).resolve().parents[1];o=NativeObserver()
def rejects(action):
    try:action()
    except (OSError,RuntimeError,ValueError):return
    raise AssertionError('invalid observation was accepted')
try:
    rows=[]
    for _ in range(5):
        before=legacy();start=time.perf_counter_ns();value=o.system();elapsed=(time.perf_counter_ns()-start)/1e6;after=legacy()
        assert value['locked'] in (before['locked'],after['locked'])
        assert value['pressure'] in (before['pressure'],after['pressure'])
        if before['swap_mib']==after['swap_mib']:assert abs(value['swap_mib']-before['swap_mib'])<=0.006
        rows.append(dict(native=value,before=before,after=after,native_ms=elapsed))
    children=[subprocess.Popen(['/bin/sleep','5'])for _ in range(2)]
    try:
        found={pid:path for pid,ppid,path in o.processes(os.getpid())};assert all(c.pid in found and found[c.pid]=='/bin/sleep'for c in children)
        assert os.getpid()in found and Path(found[os.getpid()]).exists()
    finally:
        for c in children:c.terminate();c.wait()
    assert not any(pid in [c.pid for c in children]for pid,_,_ in o.processes(os.getpid()))
    rejects(lambda:o.processes(0));rejects(lambda:o.processes(1<<32));rejects(lambda:o.processes(True));rejects(lambda:o.path(1<<40));rejects(lambda:o.path(-1));rejects(lambda:o._read_value(b'bb.private.nonexistent.sysctl',C.c_int))
    original=o.io.IORegistryGetRootEntry;o.io.IORegistryGetRootEntry=lambda _:0
    try:rejects(o.system)
    finally:o.io.IORegistryGetRootEntry=original
    original=o.io.IORegistryEntryCreateCFProperty;o.io.IORegistryEntryCreateCFProperty=lambda *args:None
    try:rejects(o.system)
    finally:o.io.IORegistryEntryCreateCFProperty=original
    original=o.io.IORegistryEntryCreateCFProperty;o.io.IORegistryEntryCreateCFProperty=lambda *args:o.cf.CFStringCreateWithCString(None,b'not a Boolean',0x08000100)
    try:rejects(o.system)
    finally:o.io.IORegistryEntryCreateCFProperty=original
    original=o.proc.proc_listchildpids;o.proc.proc_listchildpids=lambda *args:o.MaxChildren
    try:rejects(lambda:o.processes(os.getpid()))
    finally:o.proc.proc_listchildpids=original
    original=o.lib.sysctlbyname
    def short(key,buffer,length,*args):C.cast(length,C.POINTER(C.c_size_t))[0]=1;return 0
    o.lib.sysctlbyname=short
    try:rejects(o.system)
    finally:o.lib.sysctlbyname=original
    # Repeated native reads must not accumulate an IORegistry right/property.
    for _ in range(100):o.system()
    out=dict(passed=True,rows=rows,median_native_ms=statistics.median(r['native_ms']for r in rows),negative_cases=['root unavailable','property missing','wrong CF type','child capacity','short sysctl struct','bad PID','missing sysctl'],pid_count_semantics_verified=True)
    dest=p/'evidence'/('native-observer-unit-'+str(time.time_ns())+'.json');dest.write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(dict(evidence=str(dest),passed=True,median_native_ms=out['median_native_ms'])))
finally:o.close()
rejects(o.system)
