// SPDX-License-Identifier: GPL-3.0-or-later
#include "dev_travel_hooks.h"
#include <cassert>
#include <cstdio>
using namespace BbArmTravel;
int main(){
 std::vector<B> image(16384);memcpy(image.data()+0x2800,TickCode.data(),TickCode.size());memcpy(image.data()+0x2c00,WarpCode.data(),WarpCode.size());const auto original=image;
 const U base=0xf000000000,stub=base+0x10000,veneer=base+0x20000;
 auto plan=Prepare(image,base,stub,0x2800,0x2c00,veneer,16384);
 assert(image==original&&plan.patch.size()==17&&plan.patch[0]==0xe9&&plan.stub.size()<16384);
 int32_t relative;memcpy(&relative,plan.patch.data()+1,4);assert(base+0x2805+relative==stub);
 unsigned refused=0;auto reject=[&](auto action){try{action();assert(false);}catch(const std::runtime_error&){++refused;}assert(image==original);};
 reject([&]{Prepare(image,base,stub,0x2800,0x2c00,0,16384);});
 reject([&]{Prepare(image,base,stub,0x2800,0x2c00,veneer,64);});
 reject([&]{Prepare(image,base,stub+0x80000000ull,0x2800,0x2c00,veneer,16384);});
 reject([&]{Prepare(image,UINT64_MAX-3,stub,0x2800,0x2c00,veneer,16384);});
 reject([&]{Prepare(image,base,base+0x2000,0x2800,0x2c00,veneer,16384);});
 reject([&]{Prepare(image,base,stub,0x2800,UINT64_MAX-8,veneer,16384);});
 reject([&]{Prepare(image,base,stub,0x2800,0x2800,veneer,16384);});
 auto bad=image;bad[0x2c00]^=1;try{Prepare(bad,base,stub,0x2800,0x2c00,veneer,16384);assert(false);}catch(const std::runtime_error&){++refused;}
 assert(refused==8);printf("TRAVEL entry planned, %zu bytes; 8 invalid plans rejected without image writes\n",plan.stub.size());
}
