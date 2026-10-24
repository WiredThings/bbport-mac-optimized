// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string_view>
#include <algorithm>
namespace BbGuestFrame {
using U=uint64_t;
constexpr uint8_t Submit=1,Flip=2;
constexpr uint8_t Classify(std::string_view name){
 if(name=="gpu.sceGnmSubmitAndFlipCommandBuffers"||name=="gpu.sceVideoOutSubmitFlip")return Submit|Flip;
 if(name=="gpu.sceGnmSubmitCommandBuffers"||name=="gpu.sceGnmSubmitDone")return Submit;
 return 0;
}
struct Limiter {U last{},period{};bool seen{};explicit Limiter(U p):period(p){};
 void Reset(){seen=false;}
 bool Due(U now){if(!period)return false;if(!seen||now<last||now-last>=period){seen=true;last=now;return true;}return false;}
};
struct State {Limiter marker{40000000},status{1000000000};U calls{},completed{},invalid{},markers{},last_frame_mono{},last_frame_cpu{};bool frame_anchor{};};
inline U Pack(State&s){return std::min<U>(s.calls,UINT32_MAX)|(std::min<U>(s.invalid,UINT16_MAX)<<32)|(std::min<U>(s.markers,UINT16_MAX)<<48);}
inline U Accounting(State&s){return std::min<U>(s.calls,UINT32_MAX)|(std::min<U>(s.completed,UINT32_MAX)<<32);}
// Clock/Sink are injected for deterministic clock-budget and rejection tests.
template<class Clock,class Sink>class Call {
 Clock&clock;Sink&sink;State&state;U operation{},start{},cpu{};uint8_t stage{};bool active{};
public:
 Call(Clock&c,Sink&s,State&t,U op,uint8_t flags,bool enabled):clock(c),sink(s),state(t),operation(op),stage(flags),active(enabled&&flags){if(active){++state.calls;start=clock.Mono();cpu=clock.Cpu();}else{state.frame_anchor=false;state.marker.Reset();}}
 void End(){
  if(!active)return;active=false;++state.completed;const U end=clock.Mono(),used=clock.Cpu();
  if(!start||!cpu||!end||!used||end<start||used<cpu||used-cpu>end-start+1000000){++state.invalid;state.marker.Reset();state.frame_anchor=false;}
  else {if(end-start>=5000000)sink.Span(start,end-start,used-cpu,operation);if((stage&Flip)&&state.marker.Due(end)){
    if(state.frame_anchor){
     if(end>state.last_frame_mono&&used>=state.last_frame_cpu&&used-state.last_frame_cpu<=end-state.last_frame_mono+1000000)
      sink.FrameWindow(state.last_frame_mono,end-state.last_frame_mono,used-state.last_frame_cpu,operation);
     else ++state.invalid;
    }
    state.frame_anchor=true;state.last_frame_mono=end;state.last_frame_cpu=used;++state.markers;sink.Marker(end,operation);
   }}
  if(end&&state.status.Due(end))sink.Status(end,Pack(state),Accounting(state));
 }
 ~Call(){End();}
};
}
