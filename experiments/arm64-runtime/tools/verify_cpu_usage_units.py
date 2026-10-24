#!/usr/bin/env python3
"""Verify rusage Mach ticks -> nanoseconds against process CPU clock. Run without games."""
import ctypes,json,os,time
from pathlib import Path
from run_game import Usage,libproc,TIMEBASE
P=Path(__file__).resolve().parents[1]
def main():
 rows=[]
 for _ in range(3):
  before=Usage();assert libproc.proc_pid_rusage(os.getpid(),0,ctypes.byref(before))==0
  start=time.process_time_ns();value=1
  while time.process_time_ns()-start<120000000:value=(value*1664525+1013904223)&0xffffffff
  end=time.process_time_ns();after=Usage();assert libproc.proc_pid_rusage(os.getpid(),0,ctypes.byref(after))==0
  ticks=after.user+after.system-before.user-before.system;ns=ticks*TIMEBASE.numer//TIMEBASE.denom;ratio=ns/(end-start);assert .97<ratio<1.03
  rows.append(dict(process_cpu_clock_ns=end-start,rusage_mach_ticks=ticks,converted_cpu_ns=ns,ratio=ratio))
 print(json.dumps(dict(passed=True,rows=rows,mach_timebase=dict(numer=TIMEBASE.numer,denom=TIMEBASE.denom))))
if __name__=='__main__':main()
