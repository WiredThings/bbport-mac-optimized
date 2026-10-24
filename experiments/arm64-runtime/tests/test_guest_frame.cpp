#include "bb_guest_frame_policy.h"
#include "bb_guest_frame_audit.h"
#include "core/guest_cpu/fex_hle_bridge.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <chrono>
#include <string>
using namespace BbGuestFrame;
struct FakeClock {U mono=1000000000,cpu=100000000,reads{};U Mono(){++reads;return mono;}U Cpu(){++reads;return cpu;}};
struct FakeSink {unsigned spans{},markers{},statuses{},windows{};U wall{},cpu{},window_wall{},window_cpu{};void FrameWindow(U,U w,U c,U){++windows;window_wall=w;window_cpu=c;}void Span(U,U w,U c,U){++spans;wall=w;cpu=c;}void Marker(U,U){++markers;}void Status(U,U,U){++statuses;}};
static unsigned called;
static int fake_submit(){++called;std::this_thread::sleep_for(std::chrono::milliseconds(30));return 7;}
int main(int argc,char**){
 assert(Classify("gpu.sceGnmSubmitAndFlipCommandBuffers")==3&&Classify("gpu.sceVideoOutSubmitFlip")==3&&Classify("gpu.sceGnmSubmitCommandBuffers")==1&&Classify("gpu.sceGnmSubmitDone")==1);
 for(auto s:{"gpu.sceGnmSubmitDoneX","xgpu.sceGnmSubmitCommandBuffers","runtime.kernel_usleep","gpu.sceGnmSetVsShader"})assert(!Classify(s));
 FakeClock clock;FakeSink sink;State st;
 for(unsigned i=0;i<10000;++i){Call c(clock,sink,st,42,3,false);}assert(!clock.reads&&!sink.spans&&!st.calls&&!st.completed);
 {Call c(clock,sink,st,42,3,true);clock.mono+=60000000;clock.cpu+=1000000;c.End();c.End();}assert(sink.spans==1&&sink.wall==60000000&&sink.cpu==1000000&&sink.markers==1&&sink.statuses==1);
 for(unsigned i=0;i<100;++i){Call c(clock,sink,st,42,3,true);clock.mono+=1000000;clock.cpu+=1000000;}assert(sink.markers==3&&st.calls==101&&st.completed==101); // at most one marker/40ms
 {Call c(clock,sink,st,42,1,true);clock.mono+=6000000;clock.cpu+=6000000;}assert(sink.spans==2&&sink.cpu==6000000&&sink.markers==3);
 {Call c(clock,sink,st,42,3,true);--clock.mono;}assert(st.invalid==1);
 {Call c(clock,sink,st,42,3,true);clock.mono+=1000;clock.cpu+=100000000;}assert(st.invalid==2);
 assert(sink.windows==2);
 {FakeClock c;FakeSink s;State t;
  {Call x(c,s,t,1,3,true);}assert(s.windows==0);
  c.mono+=150000000;c.cpu+=1000000;{Call x(c,s,t,1,3,true);}
  assert(s.windows==1&&s.window_wall==150000000&&s.window_cpu==1000000);
  c.mono+=150000000;c.cpu+=149000000;{Call x(c,s,t,1,3,true);}
  assert(s.windows==2&&s.window_cpu==149000000);
  const auto reads=c.reads;{Call x(c,s,t,1,3,false);}assert(c.reads==reads&&!t.frame_anchor);
  c.mono+=100000000;{Call x(c,s,t,1,3,true);}assert(s.windows==2); // no interval across disabled gate
  c.mono+=50000000;c.cpu-=1;{Call x(c,s,t,1,3,true);}assert(t.invalid==1&&s.windows==2);
 }
 const bool disabled=argc>1;const auto root=std::string("/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/state/guest-frame-unit-")+std::to_string(getpid());setenv("BB_STUTTER_AUDIT",root.c_str(),1);unsetenv("BB_STUTTER_GATE");if(disabled)unsetenv("BB_GUEST_FRAME_AUDIT");else setenv("BB_GUEST_FRAME_AUDIT","1",1);assert(BbAudit::Enabled());
 using namespace Core::GuestCpu;HleCallRegistry registry;auto adapter=registry.Register(MakeHleCallAdapter(fake_submit),"gpu.sceGnmSubmitAndFlipCommandBuffers");assert(adapter->guest_frame_stage==3&&!adapter->stutter_wait);HleGuestBridge bridge(registry,[](void*,uintptr_t,size_t,bool){return false;},nullptr);HleCallFrame frame{};frame.operation=adapter->Operation();auto result=bridge.Invoke(frame);assert(std::holds_alternative<bool>(result)&&std::get<bool>(result)&&called==1&&frame.gpr[0]==7);
 bool span=false,marker=false,status=false;for(const auto&r:BbAudit::Get().data->rows)if(r.sequence){if(r.kind==BbAudit::GuestSubmit){assert(r.arg==frame.operation&&r.wall>=20000000&&r.cpu<r.wall);span=true;}marker|=r.kind==BbAudit::GuestFlipMarker;status|=r.kind==BbAudit::GuestFrameStatus;}
 assert(span==!disabled&&marker==!disabled&&status==!disabled);assert(!BbAudit::Get().data->h.dropped);unlink((root+"-"+std::to_string(getpid())+".bin").c_str());puts(disabled?"guest frame audit disabled: original call/result retained, no selected records":"guest frame audit: exact classification, disabled clocks, rate/CPU/error bounds, original adapter invoked once");
}
