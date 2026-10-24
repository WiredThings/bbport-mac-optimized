// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <algorithm>
namespace BbPacketMetadata {
using U=uint64_t;
constexpr uint32_t TaskKind=1,UnknownKind=UINT32_MAX;
struct Identity {uint32_t kind=UnknownKind,bytes=0;U function=0;bool valid=false;};
// Current RasterizerCore packet wire layout is compile-time checked at its source.
// No payload capture, pointer dereference/call, allocator, or unbounded scan.
inline Identity Decode(std::span<const uint8_t> bytes){
 Identity out;if(bytes.size()>UINT32_MAX||bytes.size()<4||!bytes.data())return out;
 out.bytes=uint32_t(bytes.size());std::memcpy(&out.kind,bytes.data(),4);
 if(out.kind>5){out.kind=UnknownKind;return out;}
 if(out.kind==TaskKind){
  if(bytes.size()<16)return out;
  uint32_t size{};std::memcpy(&size,bytes.data()+4,4);
  if(size>bytes.size()-16)return out;
  std::memcpy(&out.function,bytes.data()+8,8);
 }
 out.valid=true;return out;
}
inline U PackCounts(U anchors,U slow,U rejected){return std::min<U>(anchors,UINT32_MAX)|(std::min<U>(slow,UINT16_MAX)<<32)|(std::min<U>(rejected,UINT16_MAX)<<48);}
class Reporter {
 U period{},last{};bool initialized{};
public:
 explicit Reporter(U ticks):period(ticks){}
 bool Due(U now,bool enabled){
  if(!enabled||!period){initialized=false;return false;}
  if(!initialized||now<last||now-last>=period){initialized=true;last=now;return true;}
  return false;
 }
};
}
