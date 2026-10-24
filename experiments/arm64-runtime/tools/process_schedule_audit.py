"""Diagnostic-only cumulative rusage v4 counters for already-owned QA processes.

No scheduling changes or task ports. Raw runnable/QoS counters are retained:
process-wide totals are not the off-CPU time of one game thread or frame.
"""
import ctypes as C
import errno,os,time
FIELDS='''user_time system_time pkg_idle_wkups interrupt_wkups pageins wired_size resident_size phys_footprint proc_start_abstime proc_exit_abstime child_user_time child_system_time child_pkg_idle_wkups child_interrupt_wkups child_pageins child_elapsed_abstime diskio_bytesread diskio_byteswritten cpu_time_qos_default cpu_time_qos_maintenance cpu_time_qos_background cpu_time_qos_utility cpu_time_qos_legacy cpu_time_qos_user_initiated cpu_time_qos_user_interactive billed_system_time serviced_system_time logical_writes lifetime_max_phys_footprint instructions cycles billed_energy serviced_energy interval_max_phys_footprint runnable_time'''.split()
class Usage4(C.Structure):
    _fields_=[('uuid',C.c_uint8*16)]+[(n,C.c_uint64)for n in FIELDS]
assert C.sizeof(Usage4)==296
class ProcessScheduleAudit:
    def __init__(self):
        self.proc=C.CDLL('/usr/lib/libproc.dylib',use_errno=True)
        self.proc.proc_pid_rusage.argtypes=[C.c_int,C.c_int,C.c_void_p];self.proc.proc_pid_rusage.restype=C.c_int
    def read(self,pid):
        if type(pid)is not int or not 0<pid<=0x7fffffff:raise ValueError('invalid PID')
        result=Usage4();start=time.clock_gettime_ns(time.CLOCK_MONOTONIC);C.set_errno(0)
        status=self.proc.proc_pid_rusage(pid,4,C.byref(result));finish=time.clock_gettime_ns(time.CLOCK_MONOTONIC)
        if status:
            error=C.get_errno()
            if error in (errno.ESRCH,errno.ENOENT):return None
            raise OSError(error or errno.EIO,os.strerror(error or errno.EIO),'proc_pid_rusage v4')
        if not result.proc_start_abstime:raise RuntimeError('missing process identity')
        return dict(pid=pid,start_mono_ns=start,end_mono_ns=finish,process_start_mach=result.proc_start_abstime,uuid=bytes(result.uuid).hex(),counters={n:int(getattr(result,n))for n in FIELDS if n not in ('proc_start_abstime','proc_exit_abstime')},scope='cumulative process-wide SDK v4; CPU/runnable/QoS raw values retained; not per-frame wait attribution')
    def sample(self,pids):
        return [row for pid in sorted(set(pids))if(row:=self.read(pid))is not None]
