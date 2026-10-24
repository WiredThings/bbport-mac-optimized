// SPDX-License-Identifier: GPL-2.0-or-later
#include "extra_heaps.h"
#include <cassert>
#include <cstdio>
int main(){using namespace BbExtraHeaps;std::vector<B> image(0x4000);for(auto at:FixtureOffsets)memcpy(image.data()+at,Prefix.data(),Prefix.size());auto before=image;std::array<U,3> targets={0xf100000000,0xf100000100,0xf100000200};U base=0xf000000000,stub=base+0x10000;
 auto plan=Prepare(image,base,stub,16384,FixtureOffsets,targets);assert(image==before&&plan.patches.size()==5&&plan.stubs.size()<4096);
 unsigned refused=0;auto reject=[&](auto fn){try{fn();assert(false);}catch(const std::runtime_error&){++refused;}assert(image==before);};
 reject([&]{Prepare(image,base,stub,64,FixtureOffsets,targets);});reject([&]{Prepare(image,base,stub+0x80000000,16384,FixtureOffsets,targets);});reject([&]{Prepare(image,base,base+0x100,16384,FixtureOffsets,targets);});reject([&]{auto s=FixtureOffsets;s[1]=s[0];Prepare(image,base,stub,16384,s,targets);});reject([&]{auto s=FixtureOffsets;s[0]=0x4000;Prepare(image,base,stub,16384,s,targets);});reject([&]{auto t=targets;t[0]=0;Prepare(image,base,stub,16384,FixtureOffsets,t);});reject([&]{ValidateProfiles(image);});assert(refused==7);printf("EXTRA HEAPS 5 sites, %zu bytes, 7 transaction rejections passed\n",plan.stubs.size());}
