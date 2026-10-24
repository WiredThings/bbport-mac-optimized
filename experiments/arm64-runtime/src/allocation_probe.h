// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_resource_detours.h"
namespace BbAllocationProbe {
using namespace BbArmHooks;
constexpr U BufferReturn=0x216d4ce,TextureReturn=0x2171e9d;
inline const std::vector<B> Original{0xba,9,0,0,0,0x48,0x85,0xc0};
inline const std::vector<B> TextureOriginal{0x48,0x89,0xc2,0xb9,9,0,0,0,0x48,0x85,0xd2};
inline Plan Prepare(std::span<const B> image,U base,U stub,U site,U target,size_t capacity,unsigned kind=1){
 Require(kind==1||kind==2,"invalid allocation probe kind");
 const auto& expected=kind==1?Original:TextureOriginal;const size_t covered=kind==1?5:8;
 Require(base&&image.size()<=UINT64_MAX-base&&stub&&target,"invalid allocation probe addresses");
 Require(site<=image.size()&&expected.size()<=image.size()-site&&!memcmp(image.data()+site,expected.data(),expected.size()),"allocation probe original mismatch");
 Require(capacity<=UINT64_MAX-stub&&(stub>=base+image.size()||base>=stub+capacity),"allocation probe overlaps image");
 Emitter e;e.SaveFull();e.Bytes({0x48,0x89,0xc7}); // rax result
 if(kind==1)e.Bytes({0x49,0x8b,0x34,0x24,0x4c,0x89,0xea}); // SAVED r12 allocator, r13 bytes
 else e.Bytes({0x4c,0x89,0xee,0x48,0x89,0xda}); // r13 allocator, rbx bytes
 e.Bytes({0xb9});e.Value(uint32_t(kind));
 e.CallAbs(target);e.RestoreFull();e.code.insert(e.code.end(),expected.begin(),expected.begin()+covered);
 U next=stub+e.code.size()+5;e.Bytes({0xe9});e.Value(Relative(next,base+site+covered));Require(e.code.size()<=capacity,"allocation probe capacity");
 std::vector<B> patch(covered,0x90);patch[0]=0xe9;auto rel=Relative(base+site+5,stub);memcpy(patch.data()+1,&rel,4);
 return {std::move(e.code),{{site,std::move(patch)}},{}};
}
}
