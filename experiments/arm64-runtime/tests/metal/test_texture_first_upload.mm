// SPDX-License-Identifier: GPL-2.0-or-later
// Reuse authored mapping/real Metal setup, never game assets or runtime caches.
#define main previous_expansion_main
#include "test_texture_expansion.mm"
#undef main

int main(int argc,char**argv) {
 assert(argc==2);const std::string_view kind=argv[1];
 assert(kind=="unchanged-neighbour"||kind=="changed-cpu"||kind=="gpu-dirty");
 setenv("BB_GUEST_IN_PLACE","0",1);
 void* backing=MAP_FAILED;
 for(uintptr_t a=0x800000000ull;a<0x10000000000ull;a+=0x100000000ull){
  backing=mmap((void*)a,0x800000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED_NOREPLACE,-1,0);
  if(backing!=MAP_FAILED)break;
 }
 assert(backing!=MAP_FAILED);test_base=(uintptr_t)backing;bool pass=false;
 @autoreleasepool {
  Fixture f;auto desc=Desc(test_base+0x100000,64,AmdGpu::ImageType::Color2D,64);
  desc.type=TextureCache::BindingType::Storage;
  memset((void*)desc.info.guest_address,0x11,desc.info.guest_size);
  const auto id=f.textures.FindImage(desc);(void)f.textures.FindTexture(id,desc);f.runtime.Finish();
  auto& image=f.textures.GetImage(id);
  assert(True(image.flags&ImageFlagBits::GpuModified));
  assert(False(image.flags&ImageFlagBits::Dirty));
  assert(Pixels(f,image,0,0x11));
  vk::ClearValue red{};red.color.float32[0]=1;red.color.float32[3]=1;
  f.runtime.ClearImage(&image,{.base={0,0},.extent={1,1}},red);f.runtime.Finish();
  assert(Pixels(f,image,0,0,true));
  // A conservative CPU-dirty mark may cover a neighbour on the same host page.
  // It does not make unchanged backing authoritative over GPU-generated texels.
  if(kind=="changed-cpu") {
   memset((void*)desc.info.guest_address,0x77,desc.info.guest_size);
   f.buffers.InvalidateMemory(desc.info.guest_address,desc.info.guest_size);
  }
  image.flags|=ImageFlagBits::CpuDirty;
  if(kind=="gpu-dirty")image.flags|=ImageFlagBits::GpuDirty;
  desc.type=TextureCache::BindingType::Texture;
  (void)f.textures.FindTexture(id,desc);f.runtime.Finish();
  pass=Pixels(f,image,0,kind=="changed-cpu"?0x77:0x11,kind=="unchanged-neighbour");
  printf("FIRST_UPLOAD kind=%s pixels=%s hash=%#llx flags=%#x\n",argv[1],pass?"PASS":"FAIL",(unsigned long long)image.mip_hashes[0],u32(image.flags));
 }
 munmap(backing,0x800000);return pass?0:7;
}
