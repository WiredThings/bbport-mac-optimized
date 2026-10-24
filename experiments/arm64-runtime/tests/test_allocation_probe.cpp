// SPDX-License-Identifier: GPL-3.0-or-later
#include "allocation_probe.h"
#include <cassert>
#include <cstdio>
int main(){using namespace BbAllocationProbe;std::vector<B> image(16384);memcpy(image.data()+0x2e20,Original.data(),Original.size());memcpy(image.data()+0x2f20,TextureOriginal.data(),TextureOriginal.size());const auto before=image;U base=0xf000000000,stub=base+0x10000,target=base+0x20000;
 auto plan=Prepare(image,base,stub,0x2e20,target,16384);assert(image==before&&plan.patches.size()==1&&plan.patches[0].bytes.size()==5&&plan.stubs.size()<16384);
 auto texture=Prepare(image,base,stub,0x2f20,target,16384,2);assert(texture.patches[0].bytes.size()==8&&image==before);
 unsigned refused=0;auto reject=[&](auto fn){try{fn();assert(false);}catch(const std::runtime_error&){++refused;}assert(image==before);};
 reject([&]{Prepare(image,base,stub,0x2e20,0,16384);});reject([&]{Prepare(image,base,stub,0x2e20,target,64);});reject([&]{Prepare(image,base,base+0x100,0x2e20,target,16384);});reject([&]{Prepare(image,base,stub+0x80000000ull,0x2e20,target,16384);});reject([&]{Prepare(image,base,stub,0x2e21,target,16384);});reject([&]{Prepare(image,base,stub,UINT64_MAX-8,target,16384);});assert(refused==6);puts("ALLOCATION PROBE transactional original/bounds/overlap/rel32 checks passed");}
