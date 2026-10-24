#include "bb_typed_adapter.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
using namespace Core::GuestCpu;
static int failures,calls;static size_t page;static char* memory;static bool allow_write=true;
static void check(bool ok,const char*name){std::printf("%s %s\n",ok?"PASS":"FAIL",name);failures+=!ok;}
// The red control records a forbidden entry without actually corrupting its guard page.
static int recv_fake(int,void*dst,uint64_t length){++calls;if(dst&&length<=8)std::memset(dst,0x5a,length);return int(length);}
static int send_fake(int,const void*,uint64_t length){++calls;return int(length);}
static HleCallFrame frame(uintptr_t address,uint64_t length) {
 HleCallFrame f{};f.gpr[7]=123;f.gpr[6]=address;f.gpr[2]=length;
 f.validate_range=[](void*,uintptr_t a,size_t n,bool writable){auto base=reinterpret_cast<uintptr_t>(memory);return (!writable||allow_write)&&a>=base&&a<base+page&&n<=base+page-a;};return f;
}
static bool ok(const HleCallResult&r){return std::holds_alternative<bool>(r)&&std::get<bool>(r);}
static bool rejected(const HleCallResult&r){auto*f=std::get_if<HleCallFailure>(&r);return f&&f->error==EFAULT;}
int main(){
 page=getpagesize();memory=(char*)mmap(nullptr,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);if(memory==MAP_FAILED||mprotect(memory+page,page,PROT_NONE))return 2;
 using B=BbNet::ByteSpan;const auto end=reinterpret_cast<uintptr_t>(memory+page-8);
 for(auto rule:{BbNet::ByteSpanRule("sceHttpReadData"),BbNet::ByteSpanRule("sceNetRecv")}) {
  check(rule==B::OutputArg1Length2,"known output span rule");BbNet::TypedAdapter<decltype(&recv_fake),B::OutputArg1Length2> adapter(recv_fake,0);
  auto f=frame(end,8);int before=calls;check(ok(adapter.Invoke(f))&&calls==before+1&&memory[page-1]==0x5a,"exact boundary output passes and writes");
  for(auto n:{uint64_t(9),uint64_t(0x10000),UINT64_MAX}){f=frame(end,n);before=calls;check(rejected(adapter.Invoke(f))&&calls==before,"cross-boundary/overflow output rejected before host");}
  f=frame(0,1);before=calls;check(rejected(adapter.Invoke(f))&&calls==before,"null nonempty output rejected");
  f=frame(0,0);before=calls;check(ok(adapter.Invoke(f))&&calls==before+1,"null empty span preserved for API validation");
  allow_write=false;f=frame(end,8);before=calls;check(rejected(adapter.Invoke(f))&&calls==before,"output to read-only range rejected");allow_write=true;
 }
 for(auto rule:{BbNet::ByteSpanRule("sceHttpSendRequest"),BbNet::ByteSpanRule("sceNetSend")}) {
  check(rule==B::InputArg1Length2,"known input span rule");BbNet::TypedAdapter<decltype(&send_fake),B::InputArg1Length2> adapter(send_fake,0);
  allow_write=false;auto f=frame(end,8);int before=calls;check(ok(adapter.Invoke(f))&&calls==before+1,"read-only input range accepted");
  f=frame(end,9);before=calls;check(rejected(adapter.Invoke(f))&&calls==before,"cross-boundary input rejected before host");allow_write=true;
 }
 check(BbNet::ByteSpanRule("sceHttpWaitRequest")==B::None,"opaque HTTP wait handle has no byte-span rule");
 munmap(memory,2*page);std::printf("byte-span failures=%d\n",failures);return failures?1:0;
}
