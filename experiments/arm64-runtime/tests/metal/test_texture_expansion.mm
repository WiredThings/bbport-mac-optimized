// SPDX-License-Identifier: GPL-2.0-or-later
// Real TextureCache::FindImage and Metal image views, without game data or disk caches.
#import <Metal/Metal.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <sys/mman.h>
#include <cstring>
#include <chrono>
#include <thread>
#include "bbport_resource_lifetime.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/page_manager.h"
#include "video_core/renderer_metal/mtl_device_impl.h"
#include "video_core/renderer_metal/mtl_runtime.h"
#include "video_core/renderer_metal/mtl_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"
#include "test_mapping_rasterizer.h"

// Only this test has synthetic guest backing, mapped below. All other runtime services
// retain the shared tests' fail-fast stubs; no real guest VMAs or game files are involved.
#define runtime_memory_clamp unused_memory_clamp
#define runtime_memory_read_backing unused_memory_read_backing
#define runtime_memory_gpu_protect unused_memory_gpu_protect
#define runtime_memory_set_write_watch unused_memory_set_write_watch
#define runtime_memory_region unused_memory_region
#include "test_gpu_runtime_stubs.cpp"
#undef runtime_memory_clamp
#undef runtime_memory_read_backing
#undef runtime_memory_gpu_protect
#undef runtime_memory_set_write_watch
#undef runtime_memory_region
static uintptr_t test_base;
// Controlled mapping model for the cache/Metal integration. The separate real-runtime
// mapping-retirement test proves physical alias exclusion and concurrent map serialization.
static bool test_mapping_alias;
static unsigned test_mapping_callbacks;
extern "C" int runtime_memory_reclaim_if_unaliased_v2(uintptr_t a,uint64_t n,int(*callback)(void*),void* context) {
    assert(a>=test_base && a+n<=test_base+0x800000);
    if(test_mapping_alias)return 0;
    ++test_mapping_callbacks;
    return callback(context);
}

extern "C" {
int runtime_memory_region(uintptr_t address, uintptr_t* start, uintptr_t* end, int* mapped) {
    assert(address>=test_base && address<test_base+0x800000);
    *start=test_base;*end=test_base+0x800000;*mapped=1;return 1;
}
uint64_t runtime_memory_clamp(uintptr_t address, uint64_t size) {
    assert(address>=test_base && address+size<=test_base+0x800000); return size;
}
void runtime_memory_read_backing(uintptr_t address, void* out, uint64_t size) {
    runtime_memory_clamp(address,size);std::memcpy(out,(void*)address,size);
}
void runtime_memory_gpu_protect(uintptr_t address,uint64_t size,int,int) {
    runtime_memory_clamp(address,size);
}
void runtime_memory_set_write_watch(uintptr_t address,uint64_t size,int) {
    runtime_memory_clamp(address,size);
}
}

using namespace VideoCore;
struct Fixture {
    Metal::Device device;
    Metal::Scheduler scheduler{device, "texture-cache-view-test"};
    Metal::Runtime runtime{device, scheduler};
    MappingOnlyRasterizer mappings;
    PageManager tracker{&mappings};
    BufferCache buffers{runtime, nullptr, textures, tracker};
    TextureCache textures{runtime, nullptr, buffers, tracker};
    ~Fixture() { runtime.Finish(); }
};

static TextureCache::ImageDesc Desc(u64 address, u32 width, AmdGpu::ImageType type, u32 height=1) {
    TextureCache::ImageDesc desc;
    auto& info=desc.info;
    info.guest_address=address;
    info.pixel_format=vk::Format::eR8G8B8A8Unorm;
    info.type=type;
    info.size={width,height,1};
    info.resources={1,1};
    info.pitch=width;
    info.num_bits=32;
    info.UpdateSize();
    desc.view_info.type=type;
    desc.view_info.range.extent={1,1};
    return desc;
}


static bool Pixels(Fixture& f, Image& image, unsigned layer, unsigned char value, bool red=false) {
 f.runtime.Finish();
 @autoreleasepool {
  id<MTLCommandQueue> q=[f.device.GetImpl().device newCommandQueue];
  id<MTLBuffer> b=[f.device.GetImpl().device newBufferWithLength:64*64*4 options:MTLResourceStorageModeShared];
  id<MTLCommandBuffer> c=[q commandBuffer];id<MTLBlitCommandEncoder> e=[c blitCommandEncoder];
  [e copyFromTexture:(__bridge id<MTLTexture>)image.backing->image.native sourceSlice:layer sourceLevel:0
   sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(64,64,1) toBuffer:b destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:16384];
  [e endEncoding];[c commit];[c waitUntilCompleted];assert(c.status==MTLCommandBufferStatusCompleted);
  for(unsigned i=0;i<16384;++i)if(((unsigned char*)b.contents)[i]!=(red?(i%4==0||i%4==3?255:0):value)) {
   printf("mismatch layer=%u byte=%u actual=%u expected=%u\n",layer,i,((unsigned char*)b.contents)[i],red?(i%4==0||i%4==3?255:0):value);return false;
  }
 }
 return true;
}
int main(int argc,char**argv) {
 setenv("BB_GUEST_IN_PLACE","0",1);
 void* backing=MAP_FAILED;
 for(uintptr_t a=0x800000000ull;a<0x10000000000ull;a+=0x100000000ull){backing=mmap((void*)a,0x800000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED_NOREPLACE,-1,0);if(backing!=MAP_FAILED)break;}
 if(backing==MAP_FAILED){perror("synthetic mapping");return 8;}test_base=(uintptr_t)backing;
 const std::string_view kind=argc>1?argv[1]:"cpu-dirty";
 bool pass=false;
 @autoreleasepool {
  Fixture f;auto small=Desc(test_base+0x100000,64,AmdGpu::ImageType::Color2D,64);
  memset((void*)small.info.guest_address,0x11,32768);
  auto old_id=f.textures.FindImage(small);(void)f.textures.FindTexture(old_id,small);f.runtime.Finish();
  assert(Pixels(f,f.textures.GetImage(old_id),0,0x11));
  auto& old=f.textures.GetImage(old_id);
  const bool gpu=kind=="gpu-valid"||kind=="gpu-cpu-dirty";
  const bool dirty=kind=="cpu-dirty"||kind=="gpu-cpu-dirty";
  if(gpu){vk::ClearValue c{};c.color.float32[0]=1;c.color.float32[3]=1;f.runtime.ClearImage(&old,{.base={0,0},.extent={1,1}},c);old.flags|=ImageFlagBits::GpuModified;f.runtime.Finish();assert(Pixels(f,old,0,0,true));}
  if(dirty){f.textures.InvalidateMemory(small.info.guest_address,small.info.guest_size);f.buffers.InvalidateMemory(small.info.guest_address,32768);memset((void*)small.info.guest_address,0x77,32768);}
  auto large=small;large.info.resources.layers=2;large.info.UpdateSize();large.view_info.type=AmdGpu::ImageType::Color2DArray;large.view_info.range.extent.layers=2;
  auto new_id=f.textures.FindImage(large);assert(new_id!=old_id);
  auto& expanded=f.textures.GetImage(new_id);assert(expanded.info.resources.layers==2);
  pass=Pixels(f,expanded,0,dirty?0x77:0x11,gpu&&!dirty)&&Pixels(f,expanded,1,dirty?0x77:0x11);
  // Expansion must retain authoritative GPU ownership for later overlaps.
  if(gpu&&!dirty && False(expanded.flags&ImageFlagBits::GpuModified)){puts("missing GPU authority flag");pass=false;}
  printf("EXPAND kind=%s pixels-and-authority=%s flags=%#x\n",argv[1],pass?"PASS":"FAIL",u32(expanded.flags));
 }
 munmap(backing,0x800000);return pass?0:7;
}
