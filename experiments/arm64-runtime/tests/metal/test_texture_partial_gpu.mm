// SPDX-License-Identifier: GPL-2.0-or-later
// Authored images + completed real Metal writes to shared backing; no game files.
#define main previous_expansion_main
#include "test_texture_expansion.mm"
#undef main
static bool Pattern(Fixture& f,Image& image,size_t changed_begin,size_t changed_end){
 f.runtime.Finish();
 @autoreleasepool {
  id<MTLCommandQueue> q=[f.device.GetImpl().device newCommandQueue];
  id<MTLBuffer> b=[f.device.GetImpl().device newBufferWithLength:16384 options:MTLResourceStorageModeShared];
  id<MTLCommandBuffer> c=[q commandBuffer];id<MTLBlitCommandEncoder> e=[c blitCommandEncoder];
  [e copyFromTexture:(__bridge id<MTLTexture>)image.backing->image.native sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(64,64,1) toBuffer:b destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:16384];
  [e endEncoding];[c commit];[c waitUntilCompleted];assert(c.status==MTLCommandBufferStatusCompleted);
  for(size_t i=0;i<16384;i++){
   const unsigned expected=i>=changed_begin&&i<changed_end?0x77:(i%4==0||i%4==3?255:0);
   if(((unsigned char*)b.contents)[i]!=expected){printf("PARTIAL mismatch byte=%zu actual=%u expected=%u\n",i,((unsigned char*)b.contents)[i],expected);return false;}
  }
 }
 return true;
}
int main(int argc,char**argv){
 assert(argc==2);std::string_view kind=argv[1];assert(kind=="zero"||kind=="potential"||kind=="real-partial"||kind=="real-full"||kind=="real-interior");
 setenv("BB_GUEST_IN_PLACE","0",1);void* backing=MAP_FAILED;
 for(uintptr_t a=0x800000000ull;a<0x10000000000ull;a+=0x100000000ull){backing=mmap((void*)a,0x800000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED_NOREPLACE,-1,0);if(backing!=MAP_FAILED)break;}
 assert(backing!=MAP_FAILED);test_base=(uintptr_t)backing;bool pass=false;
 @autoreleasepool{
  Fixture f;auto desc=Desc(test_base+0x100000,64,AmdGpu::ImageType::Color2D,64);desc.type=TextureCache::BindingType::Storage;
  memset((void*)desc.info.guest_address,0x11,desc.info.guest_size);auto image_id=f.textures.FindImage(desc);(void)f.textures.FindTexture(image_id,desc);f.runtime.Finish();auto& image=f.textures.GetImage(image_id);
  vk::ClearValue red{};red.color.float32[0]=1;red.color.float32[3]=1;f.runtime.ClearImage(&image,{.base={0,0},.extent={1,1}},red);f.runtime.Finish();assert(Pattern(f,image,0,0));
  const size_t offset=kind=="real-interior"?4:0;
  const size_t bytes=kind=="zero"?0:kind=="real-full"?desc.info.guest_size:4;
  f.textures.InvalidateMemoryFromGPU(desc.info.guest_address+offset,bytes);
  const bool actual=kind.starts_with("real-");
  if(actual){
   id<MTLBuffer> shared=[f.device.GetImpl().device newBufferWithBytesNoCopy:backing length:0x800000 options:MTLResourceStorageModeShared deallocator:nil];assert(shared);
   id<MTLCommandQueue> q=[f.device.GetImpl().device newCommandQueue];id<MTLCommandBuffer> c=[q commandBuffer];id<MTLBlitCommandEncoder> e=[c blitCommandEncoder];
   [e fillBuffer:shared range:NSMakeRange(desc.info.guest_address-test_base+offset,bytes) value:0x77];[e endEncoding];[c commit];[c waitUntilCompleted];assert(c.status==MTLCommandBufferStatusCompleted);
   assert(((unsigned char*)desc.info.guest_address)[offset]==0x77);
   // Only invalidate the buffer-side CPU upload copy, not the texture: the GPU
   // invalidation above is the sole texture event under investigation.
   f.buffers.InvalidateMemory(desc.info.guest_address+offset,bytes);
  }
  desc.type=TextureCache::BindingType::Texture;(void)f.textures.FindTexture(image_id,desc);f.runtime.Finish();
  pass=Pattern(f,image,actual?offset:0,actual?offset+bytes:0);
  printf("PARTIAL kind=%s pixels=%s flags=%#x\n",argv[1],pass?"PASS":"FAIL",u32(image.flags));
 }
 munmap(backing,0x800000);return pass?0:7;
}
