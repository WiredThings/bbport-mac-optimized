// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_texture_observation.h"
#include <array>
#include <cassert>
#include <cstdio>
struct Entry {unsigned id;bool proxy;};
struct Set {unsigned count;std::array<Entry,2>entries;};
int main(){
 Set hit{2,{{{17,false},{29,true}}}};std::array<unsigned,2>views{170,290};
 Set* miss_slot=&hit;miss_slot=nullptr; // original successful cache path's documented contract
 unsigned calls=0;
 const bool ok=BbTextureObservation::MemoHit(hit,views,[&](auto i,const Entry& e,unsigned v){
  assert(miss_slot==nullptr);assert(&e==&hit.entries[i]);assert(v==e.id*10);++calls;
 });assert(ok&&calls==2&&miss_slot==nullptr);
 // Oversized hit counts and truncated view arrays must not inspect entries or emit partial sets.
 hit.count=3;calls=0;assert(!BbTextureObservation::MemoHit(hit,views,[&](auto,const auto&,auto){++calls;}));assert(!calls);
 hit.count=2;std::array<unsigned,1>short_views{170};assert(!BbTextureObservation::MemoHit(hit,short_views,[&](auto,const auto&,auto){++calls;}));assert(!calls);
 hit.count=0;std::array<unsigned,0>empty{};assert(BbTextureObservation::MemoHit(hit,empty,[&](auto,const auto&,auto){++calls;}));assert(!calls);
 // Empty/null image descriptors are observations too, not filtered into a false complete set.
 hit.count=1;hit.entries[0]={0,false};std::array<unsigned,1>null_view{0};assert(BbTextureObservation::MemoHit(hit,null_view,[&](auto,const auto& e,auto v){assert(!e.id&&!v);++calls;}));assert(calls==1);
 puts("TEXTURE_MEMO_CLEARED_OUTPUT_REGRESSION_PASS");
}
