// SPDX-License-Identifier: GPL-2.0-or-later
#include "thread_bootstrap_ranges.h"
#include "core/guest_cpu/fex_guest_cpu.h"
#include "core/guest_cpu/fex_hle_bridge.h"
#include <FEXCore/Core/X86Enums.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <unistd.h>
using namespace Core;using namespace Core::GuestCpu;
struct Mapping{void* p;size_t n=16384;Mapping(){p=mmap(nullptr,n,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);assert(p!=MAP_FAILED);}~Mapping(){munmap(p,n);}uintptr_t at(){return (uintptr_t)p;}};
unsigned ports(){mach_port_name_array_t names{};mach_msg_type_number_t nn{};mach_port_type_array_t types{};mach_msg_type_number_t nt{};assert(mach_port_names(mach_task_self(),&names,&nn,&types,&nt)==KERN_SUCCESS);if(names)vm_deallocate(mach_task_self(),(vm_address_t)names,nn*sizeof(*names));if(types)vm_deallocate(mach_task_self(),(vm_address_t)types,nt*sizeof(*types));return nn;}
int main(int argc,char** argv){
 HleCallRegistry registry;HleGuestBridge bridge(registry,nullptr,nullptr);auto made=FexGuestCpuBackend::Create(bridge);assert(std::holds_alternative<std::unique_ptr<FexGuestCpuBackend>>(made));auto cpu=std::move(std::get<std::unique_ptr<FexGuestCpuBackend>>(made));
 Mapping code,own,retired,data;const unsigned char tiny[]={0x48,0xb8,42,0,0,0,0,0,0,0,0xc3};memcpy(code.p,tiny,sizeof(tiny));assert(!mprotect(code.p,code.n,PROT_READ));
 std::vector<GuestExecutionRange> process={{code.at(),code.n,true,false},cpu->ReturnRange(),cpu->CallbackReturnRange(),{data.at(),data.n,false,true}};const auto permanent=process.size();process.push_back({retired.at(),retired.n,false,true});process.push_back({own.at(),own.n,false,true});
 GuestExecutionRequest req;req.Rip=code.at();req.Rsp=own.at()+own.n-8;auto halt=cpu->ReturnAddress();memcpy((void*)req.Rsp,&halt,8);req.Rflags=2;
 // Deterministic version of the observed race: a sibling is retired after
 // the old all-thread snapshot but before FEX checks its host permissions.
 req.MappedRanges=process;assert(!mprotect(retired.p,retired.n,PROT_NONE));
 auto fail=cpu->CreateThread(req);assert(std::holds_alternative<GuestExecutionFailure>(fail));
 if(argc>1&&std::string_view(argv[1])=="ports"){
  auto before=ports();for(int i=0;i<128;++i){auto r=cpu->CreateThread(req);assert(std::holds_alternative<GuestExecutionFailure>(r));}auto after=ports();printf("HOST MAPPING validation ports before=%u after=%u delta=%d\n",before,after,int(after)-int(before));return after>before+8?7:0;
 }
 req.MappedRanges=ThreadBootstrapRanges(process,permanent,own.at(),own.n);
 assert(req.MappedRanges.size()==permanent); // three executable ranges plus own stack
 auto result=cpu->Run(req);assert(std::holds_alternative<GuestExecutionState>(result)&&std::get<GuestExecutionState>(result).Gpr[FEXCore::X86State::REG_RAX]==42);
 assert(!mprotect(own.p,own.n,PROT_READ));auto bad=cpu->CreateThread(req);assert(std::holds_alternative<GuestExecutionFailure>(bad));assert(!mprotect(own.p,own.n,PROT_READ|PROT_WRITE));
 assert(!mprotect(data.p,data.n,PROT_NONE)); // a GPU-trapped data range is not code/stack ownership
 auto again=cpu->Run(req);assert(std::holds_alternative<GuestExecutionState>(again)&&std::get<GuestExecutionState>(again).Gpr[FEXCore::X86State::REG_RAX]==42);
 puts("THREAD BOOTSTRAP retired sibling excluded, live own stack validated, guest data not falsely declared PASS");
}
