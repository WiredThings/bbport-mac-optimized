#!/usr/bin/env python3
"""Use exact declarations of the existing private Metal frontend, never guessed ABI sizes."""
from pathlib import Path
import json,re
P=Path(__file__).resolve().parents[1];R=P/'vendor/metal_api'
meta=json.loads((P/'evidence/gpu-header-candidates.json').read_text())
headers=['#include "core/guest_cpu/hle_call_adapter.h"'];cases=[]
for header,names in meta.items():
 headers.append('#include "'+str(R/header)+'"')
 ns='Libraries::GnmDriver' if 'gnmdriver' in header else 'Libraries::VideoOut'
 for n in names:cases.append((n,ns))
headers.append('#include "'+str(R/'shadps4/core/libraries/kernel/equeue.h')+'"')
headers.append('#include "'+str(R/'shadps4/core/libraries/kernel/time.h')+'"')
text=(R/'shadps4/core/libraries/kernel/equeue.cpp').read_text();decl=[]
for m in re.finditer(r'([\w:*]+\s+PS4_SYSV_ABI\s+(\w+)\([^{};]*?\))\s*\{',text,re.S):
 decl.append(m[1]+';');cases.append((m[2],'Libraries::Kernel'))
headers+=['namespace Libraries::Kernel {','\n'.join(decl),'}','using namespace Core::GuestCpu;','std::shared_ptr<HleCallAdapter> bb_arm_gpu_adapter(std::string_view name,uintptr_t address) {']
for n,ns in cases:headers.append(f'if(name=="{n}") return MakeHleCallAdapter(reinterpret_cast<decltype(&{ns}::{n})>(address));')
headers+=['return {};','}'];(P/'generated/gpu_adapters.cpp').write_text('\n'.join(headers)+'\n');print('Typed GPU adapters:',len(cases))
