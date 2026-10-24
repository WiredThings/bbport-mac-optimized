// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_heap_sites.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <cstdlib>
using U=uint64_t;
extern "C" void bbprototype_configure_heap(uintptr_t,uintptr_t,uintptr_t,U(*)(uintptr_t,const U*,size_t),uintptr_t(*)(void*,unsigned),int(*)(uintptr_t,size_t,int));
namespace {
std::array<U,6> slots;std::array<void*,6> wrapper{};unsigned calls[6]{};bool permit=true;alignas(64) char buffer[256];
int valid(uintptr_t a,size_t n,int w){return permit&&a==(uintptr_t)slots.data()&&n==sizeof(slots)&&w==1;}
uintptr_t publish(void* fn,unsigned kind){assert(kind<6&&!wrapper[kind]);wrapper[kind]=fn;return 0x2100+kind*16;}
U invoke(uintptr_t target,const U* a,size_t count){
 assert(target>=0x1100&&target<0x1106);unsigned k=target-0x1100;++calls[k];
 switch(k){
 case 0:assert(count==1&&a[0]==32);return(U)buffer;
 case 1:assert(count==1&&a[0]==(U)buffer);return 0;
 case 2:assert(count==2&&a[0]==2&&a[1]==16);return(U)buffer;
 case 3:assert(count==2&&a[0]==(U)buffer&&a[1]==64);return(U)buffer;
 case 4:assert(count==2&&a[0]==64&&a[1]==128);return(U)buffer;
 case 5:assert(count==3&&a[1]==64&&a[2]==128);*(void**)a[0]=buffer;return 0;
 }
 std::abort();
}
}
int main(int argc,char** argv){
#ifdef BB_HEAP_OLD
 BbHeapSites::InstallCounters();return 10;
#else
 for(size_t i=0;i<6;++i)slots[i]=0x1100+i;
 const bool invalid=argc>1&&std::string_view(argv[1])=="invalid-range";
 const bool target=argc>1&&std::string_view(argv[1])=="invalid-target";
 if(invalid)permit=false;if(target)slots[4]=0x9000;
 auto before=slots;
 bbprototype_configure_heap(0x1000,0x2000,(uintptr_t)slots.data()-0xba190,invoke,publish,valid);
 BbHeapSites::InstallCounters();
 if(invalid||target){assert(!BbHeapSites::Counting());assert(slots==before);puts("HEAP refused invalid range/target without modifying table");return 0;}
 for(size_t i=0;i<6;++i)assert(slots[i]==0x2100+i*16);
 assert(((void*(*)(size_t))wrapper[0])(32)==buffer);
 ((void(*)(void*))wrapper[1])(buffer);
 assert(((void*(*)(size_t,size_t))wrapper[2])(2,16)==buffer);
 assert(((void*(*)(void*,size_t))wrapper[3])(buffer,64)==buffer);
 assert(((void*(*)(size_t,size_t))wrapper[4])(64,128)==buffer);
 void* out=nullptr;assert(((int(*)(void**,size_t,size_t))wrapper[5])(&out,64,128)==0&&out==buffer);
 for(auto c:calls)assert(c==1);assert(BbHeapSites::LiveAllocations()==3);
 puts("HEAP 6/6 guest callbacks and veneers verified at relocated table");return 0;
#endif
}
