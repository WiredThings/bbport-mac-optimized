"""Diagnostic-only process CPU intervals, no arguments, windows or content reads.

One ps inventory per second; retain only the previous inventory in RAM and the
largest twelve deltas on disk. New/vanished processes and centisecond resolution
make this an incomplete ownership sample, not a precise per-frame scheduler trace.
"""
from pathlib import PurePath
import re,subprocess,time

def cpu_seconds(text):
    m=re.fullmatch(r'(?:(\d+)-)?(?:(\d+):)?(\d+):(\d+(?:\.\d+)?)',text)
    if not m:raise ValueError('unsupported process CPU clock')
    days,hours,minutes,seconds=m.groups();seconds=float(seconds)
    if seconds>=60 or (hours is not None and int(minutes)>=60):raise ValueError('invalid process CPU clock')
    return int(days or 0)*86400+int(hours or 0)*3600+int(minutes)*60+seconds

def parse(text):
    result={}
    for line in text.splitlines():
        fields=line.strip().split(None,2)
        if not fields:continue
        if len(fields)!=3 or not fields[0].isdigit()or not fields[2].startswith('/'):continue
        pid=int(fields[0]);result[pid]=(PurePath(fields[2]).name,cpu_seconds(fields[1]))
    return result

class HostCPUAudit:
    def __init__(self):self.previous=None;self.at=None
    def sample(self,owned_pids):
        start=time.clock_gettime_ns(time.CLOCK_MONOTONIC)
        text=subprocess.check_output(['/bin/ps','-axo','pid=,time=,comm='],text=True)
        finish=time.clock_gettime_ns(time.CLOCK_MONOTONIC);current=parse(text);at=(start+finish)//2
        rows=[]
        if self.previous is not None:
            seconds=(at-self.at)/1e9
            if seconds<=0:raise RuntimeError('non-monotonic host CPU sample')
            for pid,(name,total)in current.items():
                old=self.previous.get(pid)
                if old is None or old[0]!=name or total<old[1]:continue
                delta=total-old[1]
                if delta>0:rows.append(dict(pid=pid,name=name,cpu_percent=100*delta/seconds,owned_game=pid in owned_pids))
        result=dict(start_mono_ns=start,end_mono_ns=finish,previous_midpoint_mono_ns=self.at,interval_seconds=(at-self.at)/1e9 if self.at is not None else None,top=sorted(rows,key=lambda x:x['cpu_percent'],reverse=True)[:12],unmatched_new_processes=len(set(current)-set(self.previous or {})),unmatched_vanished_processes=len(set(self.previous or {})-set(current)),scope='coarse process CPU time; centisecond precision; new/vanished processes omitted; no arguments or content')
        self.previous=current;self.at=at;return result
