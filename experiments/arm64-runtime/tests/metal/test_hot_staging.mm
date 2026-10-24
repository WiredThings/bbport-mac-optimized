// SPDX-License-Identifier: GPL-2.0-or-later
// Headless real Metal copies: unfinished ranges remain protected, hot reuse
// keeps a smaller active subset without removing any retained staging capacity.
#ifdef NDEBUG
#error "Metal regression assertions must be enabled"
#endif
#import <Metal/Metal.h>
#include <cassert>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>
#include "video_core/renderer_metal/mtl_device_impl.h"
#include "video_core/renderer_metal/mtl_runtime.h"
#include "video_core/renderer_metal/mtl_scheduler.h"
#include "video_core/renderer_metal/mtl_memory_stats.h"
#include "test_gpu_runtime_stubs.cpp"
using namespace VideoCore;
int main(int argc,char**argv) {
 const bool hot=argc>1&&argv[1][0]=='1';setenv("BB_MTL_HOT_STAGING_REUSE",hot?"1":"0",1);setenv("BB_MTL_ADAPTIVE_STAGING","0",1);
 constexpr u64 Size=9ull<<20;
 std::size_t active=0;unsigned errors=0;
 @autoreleasepool {
  Metal::Device device;Metal::Scheduler scheduler{device,"staging-hot-reuse-test"};Metal::Runtime runtime{device,scheduler};
  scheduler.SetErrorHandler([&](const std::string& text){++errors;std::fprintf(stderr,"GPU ERROR %s\n",text.c_str());});
  {
   Buffer readback{runtime,0,Size*4,MemoryType::HostCached,"test-staging-readback"};assert(!readback.mapped_data.empty());
   Metal::StagingPool pool{runtime};std::vector<StagingSpan> warm;std::set<const void*> natives;
   auto copy=[&](const StagingSpan& s,u64 offset){std::array<vk::BufferCopy,1> c{{{.srcOffset=s.offset,.dstOffset=offset,.size=Size}}};runtime.CopyBuffer(s.buffer,&readback,c);};
   auto verify=[&](u64 offset,unsigned char value){for(u64 k=0;k<Size;++k)assert(readback.mapped_data[offset+k]==value);};
   for(unsigned i=0;i<4;++i){auto s=pool.Request(Size,MemoryType::DeviceLocal,256);assert(s.mapped&&s.offset%256==0);std::memset(s.mapped,0x10+i,Size);copy(s,Size*i);assert(natives.insert(s.buffer->buffer.native).second);warm.push_back(s);}
   assert(!runtime.IsFreeKnown(runtime.CurrentTick()));
   auto pending=pool.Request(Size,MemoryType::DeviceLocal,256);assert(natives.insert(pending.buffer->buffer.native).second);assert(natives.size()==5); // all old ranges pending => grow, never overwrite
   runtime.Finish();runtime.RefreshTicks();pool.TickFrame();for(unsigned i=0;i<4;++i)verify(Size*i,0x10+i);
   std::set<const void*> touched;
   for(unsigned frame=0;frame<40;++frame){
    auto a=pool.Request(Size,MemoryType::DeviceLocal,256);std::memset(a.mapped,0x40+frame,Size);copy(a,0);
    auto b=pool.Request(Size,MemoryType::DeviceLocal,256);assert(a.buffer!=b.buffer);std::memset(b.mapped,0xa0+frame,Size);copy(b,Size);
    runtime.Finish();runtime.RefreshTicks();pool.TickFrame();verify(0,0x40+frame);verify(Size,0xa0+frame);
    if(frame>=20){touched.insert(a.buffer->buffer.native);touched.insert(b.buffer->buffer.native);}
   }
   active=touched.size();assert(active==(hot?2u:5u));
   // Other memory types retain the original suballocation/alignment behavior.
   auto small=pool.Request(1000,MemoryType::HostUncached,256);auto second=pool.Request(1000,MemoryType::HostUncached,256);assert(small.buffer==second.buffer&&small.offset!=second.offset&&small.offset%256==0&&second.offset%256==0);
   auto held=pool.Request(1<<20,MemoryType::DeviceLocal,0,true);auto held2=pool.Request(1<<20,MemoryType::DeviceLocal,0,true);assert(held.buffer!=held2.buffer);
   pool.FreeDeferred(held);pool.FreeDeferred(held2);runtime.Finish();runtime.RefreshTicks();pool.TickFrame();
   const auto before=Metal::MemoryStats::Read()[unsigned(Metal::MemoryStats::Kind::Staging)];
   for(unsigned i=0;i<3001;++i)pool.TickFrame();runtime.Finish();runtime.RefreshTicks();
   const auto after=Metal::MemoryStats::Read()[unsigned(Metal::MemoryStats::Kind::Staging)];
   // Released dedicated buffers account for2MiB; all six ring objects/slots stay.
   assert(after.bytes==before.bytes-2*(1<<20));assert(after.objects==before.objects-2);assert(after.discarded>0);
   auto woke=pool.Request(Size,MemoryType::DeviceLocal,256);assert(woke.mapped);std::memset(woke.mapped,0x5a,Size);copy(woke,0);runtime.Finish();runtime.RefreshTicks();verify(0,0x5a);
  }
  runtime.Finish();assert(!errors);
 }
 std::printf("HOT_STAGING_METAL mode=%d active=%zu pending-grow=1 byte-exact=1 host-alignment=1 held=1 idle-capacity-preserved=1 wake-byte-exact=1 errors=%u PASS\n",hot,active,errors);
}
