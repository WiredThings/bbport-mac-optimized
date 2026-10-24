// SPDX-License-Identifier: GPL-3.0-or-later
// Private FEX adaptation of the World Warper entry hook. No native x86 calls.
#pragma once
#include "guest_resource_detours.h"
namespace BbArmTravel {
using namespace BbArmHooks;
constexpr U TickOffset=0x2034770,WarpOffset=0x13cdf30;
inline const std::vector<B> TickCode={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x38};
inline const std::vector<B> WarpCode={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x83,0xec,0x38};
struct TickPlan {std::vector<B> stub,patch;};
inline TickPlan Prepare(std::span<const B> image,U base,U stub,U tick,U warp,U veneer,size_t capacity){
 const auto valid=[&](U at,size_t size){return at<=image.size()&&size<=image.size()-at;};
 Require(base&&image.size()<=UINT64_MAX-base&&stub&&veneer,"invalid travel addresses");
 Require(valid(tick,TickCode.size())&&valid(warp,WarpCode.size()),"travel site outside image");
 Require(!memcmp(image.data()+tick,TickCode.data(),TickCode.size())&&!memcmp(image.data()+warp,WarpCode.data(),WarpCode.size()),"travel original bytes mismatch");
 Require(tick+TickCode.size()<=warp||warp+WarpCode.size()<=tick,"travel sites overlap");
 Require(capacity<=UINT64_MAX-stub&&(stub>=base+image.size()||base>=stub+capacity),"travel stub overlaps image");
 Emitter e;e.SaveFull();e.CallAbs(veneer);e.RestoreFull();
 e.code.insert(e.code.end(),TickCode.begin(),TickCode.end());U next=stub+e.code.size()+5;e.Bytes({0xe9});e.Value(Relative(next,base+tick+TickCode.size()));
 Require(e.code.size()<=capacity,"travel stub capacity");
 std::vector<B> patch(TickCode.size(),0x90);patch[0]=0xe9;const uint32_t rel=Relative(base+tick+5,stub);memcpy(patch.data()+1,&rel,4);
 return {std::move(e.code),std::move(patch)};
}
}
