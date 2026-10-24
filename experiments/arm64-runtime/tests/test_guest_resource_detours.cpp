// SPDX-License-Identifier: GPL-2.0-or-later
#include "guest_resource_detours.h"
#include <cassert>
#include <cstdio>
using namespace BbArmHooks;
int main(){
 auto sites=AuthoredSites();std::vector<B> image(0x4000);for(auto&s:sites)memcpy(image.data()+s.offset,s.expected.data(),s.expected.size());
 const auto original=image;std::array<U,7> targets;targets.fill(0xf200000000ull);
 auto plan=Prepare(image,0xf000000000ull,0xf000010000ull,16384,sites,targets);
 assert(image==original&&plan.patches.size()==10&&plan.stubs.size()<8192);
 for(auto at:plan.originals)assert(at>=0xf000010000ull&&at<0xf000014000ull);
 for(auto&patch:plan.patches){int32_t d;memcpy(&d,patch.bytes.data()+1,4);U at=0xf000000000ull+patch.offset+5+d;assert(at>=0xf000010000ull&&at<0xf000014000ull);}
 unsigned refused=0;
 auto reject=[&](auto action){try{action();assert(false);}catch(const std::runtime_error&){++refused;}assert(image==original);};
 reject([&]{Prepare(image,0xf000000000ull,0xf100010000ull,16384,sites,targets);});
 reject([&]{Prepare(image,0xf000000000ull,0xf000010000ull,64,sites,targets);});
 reject([&]{auto s=sites;s[9].expected[7]^=1;Prepare(image,0xf000000000ull,0xf000010000ull,16384,s,targets);});
 reject([&]{auto s=sites;s[2].offset=UINT64_MAX-8;Prepare(image,0xf000000000ull,0xf000010000ull,16384,s,targets);});
 reject([&]{auto s=sites;s[1]=s[0];Prepare(image,0xf000000000ull,0xf000010000ull,16384,s,targets);});
 reject([&]{Prepare(image,0xf000000000ull,0xf000010000ull,16384,sites,targets,true);});
 reject([&]{Prepare(image,0xf000000000ull,0xf000010000ull,16384,std::span(sites).first(9),targets);});
 reject([&]{auto v=targets;v[0]=0;Prepare(image,0xf000000000ull,0xf000010000ull,16384,sites,v);});
 reject([&]{Prepare(image,UINT64_MAX-3,0x10000,16384,sites,targets);});
 reject([&]{Prepare(image,0xf000000000ull,0xf000000100ull,16384,sites,targets);});
 auto filtered=Prepare(image,0xf000000000ull,0xf000010000ull,16384,sites,targets,false,true);
 assert(filtered.patches.size()==10&&filtered.stubs.size()>plan.stubs.size()&&image==original);
 auto leaf=Prepare(image,0xf000000000ull,0xf000010000ull,16384,sites,targets,false,true,true);
 assert(leaf.patches.size()==10&&leaf.stubs.size()>filtered.stubs.size()&&image==original);
 reject([&]{Prepare(image,0xf000000000ull,0xf000010000ull,16384,sites,targets,false,false,true);});
 assert(refused==11);printf("RESOURCE DETOURS 10/10 planned, %zu stub bytes; 11 invalid/conflict plans rejected without modifying image\n",plan.stubs.size());
}
