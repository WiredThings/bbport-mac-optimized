#include "bb_packet_metadata.h"
#include <array>
#include <cassert>
#include <cstdio>
using namespace BbPacketMetadata;
int main(){
 std::array<uint8_t,32> data{};
 auto write=[&](uint32_t kind,uint32_t size,U function){std::memcpy(data.data(),&kind,4);std::memcpy(data.data()+4,&size,4);std::memcpy(data.data()+8,&function,8);};
 for(unsigned n=0;n<4;++n)assert(!Decode(std::span(data).first(n)).valid);
 write(1,0,0x12345678);for(unsigned n=4;n<16;++n)assert(!Decode(std::span(data).first(n)).valid);
 auto task=Decode(std::span(data).first(16));assert(task.valid&&task.kind==TaskKind&&task.bytes==16&&task.function==0x12345678);
 write(1,17,0x42);assert(!Decode(data).valid);write(1,16,0x42);assert(Decode(data).valid);
 write(1,UINT32_MAX,0x42);assert(!Decode(data).valid);write(1,0,0);assert(Decode(data).valid&&Decode(data).function==0);
 for(uint32_t kind:{0u,2u,3u,4u,5u}){write(kind,0,0xdeadbeef);auto x=Decode(data);assert(x.valid&&x.kind==kind&&!x.function);}
 write(6,0,0x42);assert(!Decode(data).valid);write(UINT32_MAX,0,0x42);assert(!Decode(data).valid);
 assert(PackCounts(1,2,3)==(1ull|(2ull<<32)|(3ull<<48)));
 auto max=PackCounts(UINT64_MAX,UINT64_MAX,UINT64_MAX);assert(max==UINT64_MAX); // saturation, never wraps zero
 Reporter rate(100);assert(!rate.Due(0,false));assert(rate.Due(1,true));for(U t=2;t<101;++t)assert(!rate.Due(t,true));assert(rate.Due(101,true));assert(rate.Due(4,true));assert(!rate.Due(5,true));assert(!rate.Due(6,false));assert(rate.Due(7,true));Reporter no(0);assert(!no.Due(0,true));
 puts("packet metadata: truncation/kind/size/function decode; saturated bounded counters; periodic/gate/backwards-counter behavior");
}
