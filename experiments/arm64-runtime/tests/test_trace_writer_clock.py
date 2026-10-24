"""Actual Scope publication rejects a failed/regressing wall clock, without a game."""
from pathlib import Path
import os,subprocess,tempfile
P=Path(__file__).resolve().parents[1]
source=r'''
#include "bb_stutter_audit.h"
#include <cassert>
int main(){
 auto* b=BbAudit::Get().data;assert(b);
 {BbAudit::Scope s{30,0,0};s.start=BbAudit::Now()+1000000000ULL;s.End();s.End();}
 assert(b->h.issued.load()==0);assert(b->h.dropped.load()==1);
 {BbAudit::Scope s{30,0,0};s.start=BbAudit::Now()-10000;s.End();s.End();}
 assert(b->h.issued.load()==1);assert(b->h.dropped.load()==1);
 assert(b->rows[0].sequence.load()==1 && b->rows[0].wall<1000000000ULL);
}
'''
with tempfile.TemporaryDirectory(prefix='bb-trace-writer-')as d:
 root=Path(d);cpp=root/'test.cpp';cpp.write_text(source);exe=root/'test'
 subprocess.run(['/usr/bin/clang++','-std=c++20','-O2','-I'+str(P/'src'),str(cpp),'-o',str(exe)],check=True)
 env=dict(os.environ,BB_STUTTER_AUDIT=str(root/'trace'));env.pop('BB_STUTTER_GATE',None)
 subprocess.run([str(exe)],env=env,check=True)
print('PASS writer counts clock regression as dropped; valid sample and idempotent End preserved')
