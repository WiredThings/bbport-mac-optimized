"""Read-only, bounded Darwin VM counters; diagnostic only, never a stop policy.

The rev1 prefix is checked against the installed SDK in the authored test. A
larger fixed receive buffer admits later revisions; returned count is validated.
Counters are SYSTEM-WIDE pages, not game-attributed faults or fault stall time.
"""
import ctypes as C
import os,time

class VMRev1(C.Structure):
    _fields_=[(n,C.c_uint32)for n in ('free_count','active_count','inactive_count','wire_count')]+[(n,C.c_uint64)for n in ('zero_fill_count','reactivations','pageins','pageouts','faults','cow_faults','lookups','hits','purges')]+[(n,C.c_uint32)for n in ('purgeable_count','speculative_count')]+[(n,C.c_uint64)for n in ('decompressions','compressions','swapins','swapouts')]+[(n,C.c_uint32)for n in ('compressor_page_count','throttled_count','external_page_count','internal_page_count')]+[('total_uncompressed_pages_in_compressor',C.c_uint64)]

class NativeVMObserver:
    def __init__(self):
        self.lib=C.CDLL('/usr/lib/libSystem.B.dylib')
        self.lib.mach_host_self.argtypes=[];self.lib.mach_host_self.restype=C.c_uint32
        self.lib.host_statistics64.argtypes=[C.c_uint32,C.c_int,C.c_void_p,C.POINTER(C.c_uint32)];self.lib.host_statistics64.restype=C.c_int
        self.lib.host_statistics.argtypes=[C.c_uint32,C.c_int,C.c_void_p,C.POINTER(C.c_uint32)];self.lib.host_statistics.restype=C.c_int
        self.cpu_buffer=(C.c_uint32*4)()
        self.lib.mach_port_deallocate.argtypes=[C.c_uint32,C.c_uint32];self.lib.mach_port_deallocate.restype=C.c_int
        self.task=C.c_uint32.in_dll(self.lib,'mach_task_self_').value
        self.host=self.lib.mach_host_self()
        if not self.host:raise RuntimeError('host port unavailable')
        self.buffer=(C.c_uint32*512)();self.page_bytes=os.sysconf('SC_PAGE_SIZE')
    def close(self):
        if self.host:
            port=self.host;self.host=0
            if self.lib.mach_port_deallocate(self.task,port):raise RuntimeError('host port release failed')
    def __del__(self):
        if getattr(self,'host',0):self.close()
    def system(self):
        if not self.host:raise RuntimeError('VM observer closed')
        start=time.clock_gettime_ns(time.CLOCK_MONOTONIC);count=C.c_uint32(len(self.buffer))
        status=self.lib.host_statistics64(self.host,4,self.buffer,C.byref(count))
        cpu_count=C.c_uint32(4)
        cpu_status=self.lib.host_statistics(self.host,3,self.cpu_buffer,C.byref(cpu_count))
        if cpu_status or cpu_count.value!=4:raise RuntimeError('unexpected HOST_CPU_LOAD_INFO response')
        end=time.clock_gettime_ns(time.CLOCK_MONOTONIC)
        if status or not C.sizeof(VMRev1)//4<=count.value<=len(self.buffer):raise RuntimeError('unexpected host_statistics64 response: '+repr((status,count.value)))
        value=VMRev1.from_buffer_copy(self.buffer)
        return dict(start_mono_ns=start,end_mono_ns=end,page_bytes=self.page_bytes,kernel_words=count.value,scope='system-wide',cpu_ticks=dict(zip(('user','system','idle','nice'),map(int,self.cpu_buffer))),**{name:int(getattr(value,name))for name,_ in VMRev1._fields_})
