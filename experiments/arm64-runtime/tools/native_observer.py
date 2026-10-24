#!/usr/bin/env python3
"""Single-supervisor Darwin observation; fresh kernel values, failures abort QA.
No process signaling, settings changes, cached lock state, or subprocesses.
"""
import ctypes as C
import errno,os

class SwapUsage(C.Structure):
    _fields_=[('total',C.c_uint64),('available',C.c_uint64),('used',C.c_uint64),('pagesize',C.c_uint32),('encrypted',C.c_uint32)]
assert C.sizeof(SwapUsage)==32

class NativeObserver:
    MaxChildren=4096
    def __init__(self):
        self.lib=C.CDLL('/usr/lib/libSystem.B.dylib',use_errno=True)
        self.proc=C.CDLL('/usr/lib/libproc.dylib',use_errno=True)
        self.io=C.CDLL('/System/Library/Frameworks/IOKit.framework/IOKit',use_errno=True)
        self.cf=C.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation',use_errno=True)
        specs=[(self.lib,'sysctlbyname',[C.c_char_p,C.c_void_p,C.POINTER(C.c_size_t),C.c_void_p,C.c_size_t],C.c_int),
          (self.proc,'proc_listchildpids',[C.c_int,C.c_void_p,C.c_int],C.c_int),
          (self.proc,'proc_pidpath',[C.c_int,C.c_void_p,C.c_uint32],C.c_int),
          (self.io,'IORegistryGetRootEntry',[C.c_uint32],C.c_uint32),
          (self.io,'IORegistryEntryCreateCFProperty',[C.c_uint32,C.c_void_p,C.c_void_p,C.c_uint32],C.c_void_p),
          (self.io,'IOObjectRelease',[C.c_uint32],C.c_int),
          (self.cf,'CFStringCreateWithCString',[C.c_void_p,C.c_char_p,C.c_uint32],C.c_void_p),
          (self.cf,'CFGetTypeID',[C.c_void_p],C.c_ulong),
          (self.cf,'CFBooleanGetTypeID',[],C.c_ulong),
          (self.cf,'CFBooleanGetValue',[C.c_void_p],C.c_bool),
          (self.cf,'CFRelease',[C.c_void_p],None)]
        for lib,name,args,result in specs:
            f=getattr(lib,name);f.argtypes=args;f.restype=result
        self.key=self.cf.CFStringCreateWithCString(None,b'IOConsoleLocked',0x08000100)
        if not self.key:raise RuntimeError('cannot create lock-property key')
        self._children=(C.c_int*self.MaxChildren)()
        self._path=C.create_string_buffer(4096)
    def close(self):
        if self.key:self.cf.CFRelease(self.key);self.key=None
    def __del__(self):
        if getattr(self,'key',None):self.close()
    def _read_value(self,key,value_type):
        value=value_type();length=C.c_size_t(C.sizeof(value))
        if self.lib.sysctlbyname(key,C.byref(value),C.byref(length),None,0):
            code=C.get_errno();raise OSError(code,os.strerror(code),key.decode())
        if length.value!=C.sizeof(value):raise RuntimeError('unexpected sysctl structure size: '+key.decode())
        return value
    def _read_locked(self):
        if not self.key:raise RuntimeError('observer already closed')
        root=self.io.IORegistryGetRootEntry(0)
        if not root:raise RuntimeError('cannot read IORegistry root; lock state unknown')
        value=None
        try:
            value=self.io.IORegistryEntryCreateCFProperty(root,self.key,None,0)
            if not value:raise RuntimeError('IOConsoleLocked unavailable; lock state unknown')
            if self.cf.CFGetTypeID(value)!=self.cf.CFBooleanGetTypeID():
                raise RuntimeError('IOConsoleLocked is not a Boolean')
            return bool(self.cf.CFBooleanGetValue(value))
        finally:
            if value:self.cf.CFRelease(value)
            if self.io.IOObjectRelease(root):raise RuntimeError('IORegistry root release failed')
    def system(self):
        pressure=self._read_value(b'kern.memorystatus_vm_pressure_level',C.c_int).value
        if pressure not in (1,2,4):raise RuntimeError('unknown memory-pressure level')
        locked=self._read_locked()
        swap=self._read_value(b'vm.swapusage',SwapUsage)
        if swap.used>swap.total or not swap.pagesize or swap.pagesize&(swap.pagesize-1):
            raise RuntimeError('invalid swap usage structure')
        return dict(pressure=pressure,locked=locked,swap_mib=swap.used/2**20)
    def path(self,pid):
        if type(pid)is not int or not 0<pid<=0x7fffffff:raise ValueError('invalid PID')
        C.set_errno(0);n=self.proc.proc_pidpath(pid,self._path,C.sizeof(self._path))
        if n<=0:
            code=C.get_errno()
            if code in (0,errno.ESRCH,errno.ENOENT):return None # raced ordinary exit
            raise OSError(code,os.strerror(code),str(pid))
        if n>=C.sizeof(self._path):raise RuntimeError('process path truncated')
        raw=self._path.raw[:n].split(b'\0',1)[0]
        text=os.fsdecode(raw)
        if not text.startswith('/') or '\n'in text:raise RuntimeError('invalid process path')
        return text
    def processes(self,parent):
        if type(parent)is not int or not 0<parent<=0x7fffffff:raise ValueError('invalid parent PID')
        # Unlike proc_listpids, this wrapper returns a PID COUNT, not bytes.
        # Verified against two real authored children and SDK declarations.
        C.set_errno(0);count=self.proc.proc_listchildpids(parent,self._children,C.sizeof(self._children))
        if count<0:
            code=C.get_errno();raise OSError(code,os.strerror(code),'proc_listchildpids')
        if count>=self.MaxChildren:raise RuntimeError('child PID list reached fixed capacity')
        ids=[int(self._children[i])for i in range(count)]
        if any(x<=0 or x==parent for x in ids)or len(set(ids))!=len(ids):raise RuntimeError('invalid child PID list')
        rows=[]
        for pid in [parent]+ids:
            path=self.path(pid)
            if path is not None:rows.append((pid,os.getpid()if pid==parent else parent,path))
        return rows
