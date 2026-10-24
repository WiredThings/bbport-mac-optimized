// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "guest_resource_detours.h"
#include "bbport_game_menu.h"
namespace BbArmMenu {
struct Plan{std::vector<uint8_t> stubs;std::vector<BbArmHooks::Patch> patches;std::vector<uint64_t> originals;};
inline Plan Prepare(std::span<const uint8_t> image,uint64_t base,uint64_t stubs,size_t capacity,std::span<const BbGameMenu::GuestHook> hooks){
 using namespace BbArmHooks;
 Require(base&&image.size()<=UINT64_MAX-base,"menu image overflow");
 Require(stubs&&capacity<=UINT64_MAX-stubs,"menu stubs overflow");
 Require(stubs>=base+image.size()||base>=stubs+capacity,"menu stubs overlap image");
 Require(hooks.size()==2,"menu requires both hooks");
 for(size_t i=0;i<hooks.size();++i){const auto& h=hooks[i];
  Require(h.expected&&h.target&&h.length>=5&&h.length<=32&&h.offset<=image.size()&&h.length<=image.size()-h.offset,"menu hook bounds");
  Require(!memcmp(image.data()+h.offset,h.expected,h.length),"menu original mismatch");
  for(size_t j=0;j<i;++j)Require(h.offset+h.length<=hooks[j].offset||hooks[j].offset+hooks[j].length<=h.offset,"menu hook overlap");
 }
 Plan p;
 for(auto&h:hooks){
  Emitter original;original.code.insert(original.code.end(),h.expected,h.expected+h.length);original.JumpAbs(base+h.offset+h.length);
  Require(p.stubs.size()<=capacity&&original.code.size()<=capacity-p.stubs.size(),"menu stub capacity");
  p.originals.push_back(stubs+p.stubs.size());p.stubs.insert(p.stubs.end(),original.code.begin(),original.code.end());p.stubs.resize((p.stubs.size()+15)&~size_t(15),0x90);
  Emitter thunk;thunk.JumpAbs(h.target);const auto target=stubs+p.stubs.size();
  Require(p.stubs.size()<=capacity&&thunk.code.size()<=capacity-p.stubs.size(),"menu thunk capacity");
  p.stubs.insert(p.stubs.end(),thunk.code.begin(),thunk.code.end());p.stubs.resize((p.stubs.size()+15)&~size_t(15),0x90);
  Require(p.stubs.size()<=capacity,"menu aligned capacity");
  std::vector<uint8_t> patch(h.length,0x90);patch[0]=0xe9;auto relative=Relative(base+h.offset+5,target);memcpy(patch.data()+1,&relative,4);p.patches.push_back({h.offset,std::move(patch)});
 }
 return p;
}
}
