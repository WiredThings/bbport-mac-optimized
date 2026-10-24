#include "core/guest_cpu/fex_hle_bridge.h"
#include "bb_stutter_audit.h"
#include <cassert>
#include <chrono>
#include <thread>
#include <string>
using namespace Core::GuestCpu;
static int called;static int fake_sleep(uint32_t requested){assert(requested==7);++called;std::this_thread::sleep_for(std::chrono::milliseconds(15));return 0;}
int main(){
 const auto path=std::string("/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/state/sleep-audit-")+std::to_string(getpid());setenv("BB_STUTTER_AUDIT",path.c_str(),1);unsetenv("BB_STUTTER_GATE");assert(BbAudit::Enabled());
 HleCallRegistry registry;auto adapter=registry.Register(MakeHleCallAdapter(fake_sleep),"runtime.kernel_usleep");HleGuestBridge bridge(registry,[](void*,uintptr_t,size_t,bool){return false;},nullptr);
 HleCallFrame frame{};frame.operation=adapter->Operation();frame.gpr[7]=7;auto result=bridge.Invoke(frame);assert(std::holds_alternative<bool>(result)&&std::get<bool>(result)&&called==1&&frame.gpr[0]==0);
 bool seen=false;auto* trace=BbAudit::Get().data;for(const auto&row:trace->rows)if(row.sequence&&row.kind==BbAudit::GuestSleep){assert(row.arg==7&&row.wall>=10000000&&row.cpu<row.wall);seen=true;}
 assert(seen&&trace->h.dropped==0);printf("sleep audit passed: requested 7us, observed >=10ms, no timer policy change\n");unlink((path+"-"+std::to_string(getpid())+".bin").c_str());
}
