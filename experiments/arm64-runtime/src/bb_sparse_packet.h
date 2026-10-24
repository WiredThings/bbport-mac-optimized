// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <limits>
#include <optional>
namespace BbSparsePacket {
using U=uint64_t;
struct Result {U packet_start{},packet_wall{},window_start{},window_wall{},cpu_upper{},cpu_slack{};};
// Clock exposes Mono(), Cpu(), Ticks(); no reads on disabled or normal fast packets.
class Probe {
 U frequency{},refresh{},slow{};U tick{},mono{},cpu{},skew{},pending{};bool valid{},armed{};
 bool Ns(U ticks,U& ns) const {if(!frequency)return false;auto n=(__uint128_t(ticks)*1000000000)/frequency;if(n>UINT64_MAX)return false;ns=U(n);return true;}
 void Invalidate(){valid=false;armed=false;}
public:
 U anchors{},slow_packets{},rejected{};
 explicit Probe(U f):frequency(f),refresh(U(__uint128_t(f)*5000/1000000)),slow(U(__uint128_t(f)*20000/1000000)){}
 template<class Clock>void Before(U start,bool enabled,Clock& clock){
  armed=false;
  if(!enabled||!refresh||!slow){Invalidate();return;}
  if(valid&&start<tick){++rejected;Invalidate();return;}
  if(!valid||start-tick>=refresh){
   auto m=clock.Mono(),c=clock.Cpu(),end=clock.Ticks();U span{};
   if(!m||!c||end<start||!Ns(end-start,span)||span>1000000){++rejected;Invalidate();return;}
   tick=start;mono=m;cpu=c;skew=span;valid=true;++anchors;
  }
  pending=start;armed=true;
 }
 void Start(U actual){if(!armed)return;if(actual<pending){++rejected;Invalidate();return;}pending=actual;}
 template<class Clock>std::optional<Result> After(U end,bool enabled,Clock& clock){
  if(!enabled||!armed){Invalidate();return{};}armed=false;
  if(end<pending){++rejected;Invalidate();return{};}
  if(end-pending<slow)return{};
  ++slow_packets;U wall{},lead{};
  if(!valid||!Ns(end-pending,wall)||!Ns(pending-tick,lead)||lead>6000000||mono>UINT64_MAX-lead||mono+lead>UINT64_MAX-wall){++rejected;Invalidate();return{};}
  auto m=clock.Mono(),c=clock.Cpu(),finish=clock.Ticks();U post{};
  if(!m||!c||finish<end||!Ns(finish-end,post)||post>1000000||m<mono||c<cpu){++rejected;Invalidate();return{};}
  U total{};if(!Ns(finish-tick,total)||total>UINT64_MAX-1000000||c-cpu>total+1000000){++rejected;Invalidate();return{};}
  if(lead>UINT64_MAX-skew||lead+skew>UINT64_MAX-post){++rejected;Invalidate();return{};}
  // CPU interval begins before the handler, ends afterwards. It is an upper
  // bound, with <=5ms preceding work plus <=1ms clock brackets on each end.
  return Result{mono+lead,wall,mono,m-mono,c-cpu,lead+skew+post};
 }
};
static_assert(sizeof(Probe)<=128);
}
