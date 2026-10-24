// SPDX-License-Identifier: MIT
#include "core/guest_cpu/fex_hle_bridge.h"
#include <cassert>
#include <thread>
#include <atomic>
using namespace Core::GuestCpu;
class Probe:public HleCallAdapter{
 HleCallResult Invoke(HleCallFrame& f)const override{
  auto op=f.gpr[0],a=f.gpr[1],n=f.gpr[2];bool w=f.gpr[3];
  f.gpr[0]=op==0?PublishHostRange((void*)a,n,w):op==1?RevokeHostRange((void*)a):f.validate_range(f.validate_context,a,n,w);
  return true;
 }
};
int main(){
 HleCallRegistry registry;auto probe=registry.Register(std::make_shared<Probe>(),"publication-probe");HleGuestBridge bridge(registry,nullptr,nullptr);
 auto call=[&](unsigned op,uintptr_t a,size_t n=16,bool w=true){HleCallFrame f{};f.operation=probe->Operation();f.gpr[0]=op;f.gpr[1]=a;f.gpr[2]=n;f.gpr[3]=w;assert(std::holds_alternative<bool>(bridge.Invoke(f)));return bool(f.gpr[0]);};
 constexpr uintptr_t A=0x10000,B=0x20000;
 assert(!call(2,A));assert(call(0,A,32,false));assert(!call(0,A));assert(!call(0,A+1));
 assert(call(2,A,32,false)&&!call(2,A,1,true));assert(!call(2,A+31,2,false));
 assert(!call(2,0,1,false)&&!call(2,A,0,false)&&!call(2,UINTPTR_MAX-1,8,false));
 assert(call(1,A)&&!call(2,A,1,false)&&!call(1,A));
 assert(call(0,A,32,true));
 std::atomic<bool> done=false;std::thread reader([&]{while(!done){assert(call(2,A,32,true));call(2,B,16,true);}});
 for(unsigned i=0;i<10000;++i){assert(call(0,B));assert(call(2,B));assert(call(1,B));assert(!call(2,B));}
 done=true;reader.join();assert(call(1,A));assert(!call(2,A)&&!call(2,B));
 puts("PUBLISHED RANGES readonly, overlaps, revoke and concurrent publication passed");
}
