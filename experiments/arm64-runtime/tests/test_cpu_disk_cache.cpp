// Authored, isolated feasibility fixture. Does not load game files or enable game caching.
#define main upstream_smoke_main
#include AETHER_SMOKE_SOURCE
#undef main
#include "Interface/Core/LookupCache.h"
#include "Interface/Context/Context.h"
#include <FEXCore/Utils/AllocatorHooks.h>
#include <FEXCore/HLE/SourcecodeResolver.h>
#include <CommonCrypto/CommonDigest.h>
#include "bb_fex_cache_pages.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <algorithm>
#include <set>
#include <string>

#include "cpu_cache_fixture_validation.h"

int main(int argc,char**argv){
 setvbuf(stdout,nullptr,_IONBF,0);
 if(argc!=5)return 2;
 const std::string_view mode=argv[1];const bool wide=mode=="generate-wide"||mode.starts_with("load-wide");
 const bool page_probe=mode=="load-wide-page-probe",grouped=mode=="load-wide-grouped";
 const bool generate=mode=="generate"||mode=="generate-wide",load=mode=="load"||mode=="load-wide"||page_probe||grouped;
 if(!generate&&!load&&mode!="validate")return 2;
 int fd=-1;void* file=MAP_FAILED;size_t file_size=0;
 if(!generate){
  fd=open(argv[2],O_RDONLY|O_NOFOLLOW);struct stat st{};
  if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<56||st.st_size>1024*1024)return 20;
  file_size=st.st_size;file=mmap(nullptr,file_size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);fd=-1;
  if(file==MAP_FAILED)return 20;
  std::span<const std::byte> data{static_cast<const std::byte*>(file),file_size};
  if(!digest_ok(data,argv[4])||!validate(data)){std::puts("CACHE rejected_before_FEX_load=1");munmap(file,file_size);return 21;}
  if(mode=="validate"){std::puts("CACHE validated=1 no_execute=1");munmap(file,file_size);return 0;}
 }
 ConfigScope config;
 FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLECODECACHINGWIP,"1");
 FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLELAZYCODECACHINGWIP,(page_probe||grouped)?"1":"0");
 FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLECODECACHEVALIDATION,"0");
 auto ctx=FEXCore::Context::Context::CreateNewContext(FEX::FetchHostFeatures());if(!ctx)return 3;
 SmokeSignalDelegator signals{0};auto sys=fextl::make_unique<SmokeSyscallHandler>();
 ctx->SetSignalDelegator(&signals);ctx->SetSyscallHandler(sys.get());ctx->EnableExitOnHLT();
 if(generate)ctx->GetCodeCache().InitiateCacheGeneration();
 if(!ctx->InitCore())return 4;
 const size_t page=sysconf(_SC_PAGESIZE);if(page!=16384)return 5;
 uintptr_t requested=std::strtoull(argv[3],nullptr,0);
 void* guest=mmap(reinterpret_cast<void*>(requested),page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 if(guest==MAP_FAILED||reinterpret_cast<uintptr_t>(guest)!=requested){std::printf("CACHE mapping_miss requested=%#llx actual=%p errno=%d\n",(unsigned long long)requested,guest,errno);return 6;}
 Mapping stack(page,PROT_READ|PROT_WRITE);if(!stack.IsValid())return 7;
 auto* bytes=static_cast<unsigned char*>(guest);std::memset(bytes,0xf4,page);
 const unsigned char first[]={0x48,0x8b,0x05,0,0,0,0,0x48,0x83,0xc0,7,0xe9,0,0,0,0};
 std::memcpy(bytes,first,sizeof(first));int32_t disp=0x3800-7;std::memcpy(bytes+3,&disp,4);disp=0x200-16;std::memcpy(bytes+12,&disp,4);
 const unsigned char second[]={0x48,0x83,0xc0,11,0xf4};std::memcpy(bytes+0x200,second,sizeof(second));
 uint64_t value=0x12345678;std::memcpy(bytes+0x3800,&value,8);
 if(wide){for(unsigned i=0;i<120;++i){
   size_t off=i*32;const unsigned char op[]={0x48,0x8b,0x05,0,0,0,0,0x48,0x83,0xc0,7,0xf4};
   std::memcpy(bytes+off,op,sizeof(op));int32_t d=0x3800-static_cast<int32_t>(off)-7;std::memcpy(bytes+off+3,&d,4);bytes[off+10]=7+i;
 }}
 FEXCore::ExecutableFileInfo info{};info.FileId=0x1234;info.Filename="authored-only-rip-relative";
 FEXCore::ExecutableFileSectionInfo section{info,requested,requested,requested+4096};
 auto*t=ctx->CreateThread(requested,(uint64_t)stack.Get()+page-16);if(!t)return 8;
 CallRetStack cr;if(!cr.IsReserved()||!cr.MakeWritable())return 9;cr.Initialize(t);GuestSegmentState seg;seg.Initialize(t->CurrentFrame->State);
 if(generate){
  {FEXCore::Allocator::ScopedJITWriteProtect write;if(wide){for(unsigned i=0;i<120;++i)ctx->CompileRIP(t,requested+i*32);}
   else{ctx->CompileRIP(t,requested);ctx->CompileRIP(t,requested+0x200);}}
  fd=open(argv[2],O_CREAT|O_EXCL|O_WRONLY,0600);if(fd<0)return 10;
  bool ok;{FEXCore::Allocator::ScopedJITWriteProtect write;ok=ctx->GetCodeCache().SaveData(*t,fd,section,0);}
  struct stat st{};ok=ok&&fsync(fd)==0&&fstat(fd,&st)==0;close(fd);fd=-1;
  std::printf("CACHE generated=%d bytes=%lld guest=%#llx host=%#llx\n",ok,(long long)st.st_size,(unsigned long long)requested,(unsigned long long)t->LookupCache->FindBlock(t,requested));
  ctx->DestroyThread(t);ctx.reset();munmap(guest,page);return ok?0:11;
 }
 auto& cache=ctx->GetCodeCache();
 auto loaded=cache.LoadCache({static_cast<std::byte*>(file),file_size},info,requested);if(!loaded){ctx->DestroyThread(t);ctx.reset();munmap(file,file_size);munmap(guest,page);return 12;}
 cache.RegisterMappedCodeBuffer(*loaded);
 if(!cache.EnableLoadedSection(t,*loaded,section))return 13;
 if(page_probe){
   // Inspect the real upstream selector. Do not pass an unaligned range into mach_vm_remap.
   auto selected=FEXCore::AbstractCodeCache::SelectCodeRangeToFinalize(*loaded,1,2);
   const auto offset=selected.data()-loaded->CodeBuffer.data();
   const bool host_aligned=reinterpret_cast<uintptr_t>(selected.data())%page==0;
   const bool complete_host_range=host_aligned&&(selected.size()%page==0||selected.data()+selected.size()==loaded->CodeBuffer.data()+loaded->CodeBuffer.size());
   std::printf("CACHE lazy_selected_offset=%td size=%zu host_page=%zu host_start_aligned=%d complete_host_range=%d executed=0\n",offset,selected.size(),page,host_aligned,complete_host_range);
   {FEXCore::Allocator::ScopedJITWriteProtect write;ctx->ClearCodeCache(t);}
   loaded.reset();ctx->DestroyThread(t);ctx.reset();munmap(file,file_size);munmap(guest,page);return complete_host_range?0:22;
 }
 if(grouped){
   if(BbCachePages::Select(*loaded,0,1,8192+4096).valid||BbCachePages::Select(*loaded,1,1,page).valid||
      BbCachePages::Select(*loaded,0,loaded->NumPages()+1,page).valid)return 23;
   loaded->LoadedPages[0]=true;
   const bool mixed_rejected=!BbCachePages::Select(*loaded,0,1,page).valid;
   loaded->LoadedPages[0]=false;if(!mixed_rejected)return 27;
   size_t steps=0;
   for(size_t i=0;i<loaded->NumPages();++i){
     auto pick=BbCachePages::Select(*loaded,i,i+1,page);if(!pick.valid)return 24;
     if(pick.range.empty())continue;
     const size_t off=pick.range.data()-loaded->CodeBuffer.data();
     bool safe=off%page==0&&(pick.range.size()%page==0||off+pick.range.size()==loaded->CodeBuffer.size());if(!safe)return 25;
     cache.FinalizeCodePages(*loaded,pick.range);++steps;
   }
   auto done=BbCachePages::Select(*loaded,0,loaded->NumPages(),page);
   if(!done.valid||!done.range.empty())return 26;
   std::printf("CACHE host_group_finalizations=%zu grouped_host_page=%zu\n",steps,page);
 }
 auto host=t->LookupCache->FindBlock(t,requested);
 bool hit=host>=reinterpret_cast<uintptr_t>(loaded->CodeBuffer.data())&&host<reinterpret_cast<uintptr_t>(loaded->CodeBuffer.data()+loaded->CodeBuffer.size());
 if(!hit)return 14;
 auto* impl=static_cast<FEXCore::Context::ContextImpl*>(ctx.get());
 const auto compiled_before=impl->LatestOffset;
 bool result=true;const unsigned executions=wide?120:1;
 for(unsigned i=0;i<executions;++i){
   uint64_t entry=requested+(wide?i*32:0);auto cached=t->LookupCache->FindBlock(t,entry);
   if(cached<reinterpret_cast<uintptr_t>(loaded->CodeBuffer.data())||cached>=reinterpret_cast<uintptr_t>(loaded->CodeBuffer.data()+loaded->CodeBuffer.size()))return 16;
   t->CurrentFrame->State.rip=entry;ctx->ExecuteThread(t);
   result&=t->CurrentFrame->State.gregs[FEXCore::X86State::REG_RAX]==value+(wide?7+i:18);
 }
 const bool no_jit=compiled_before==impl->LatestOffset;
 std::printf("CACHE executions=%u no_fallback_jit=%d loaded_pages=%zu host_page=%zu\n",executions,no_jit,std::count(loaded->LoadedPages.begin(),loaded->LoadedPages.end(),true),page);
 result&=no_jit;
 std::printf("CACHE loaded_hit=%d result_correct=%d guest=%#llx host=%#llx code_bytes=%zu\n",hit,result,(unsigned long long)requested,(unsigned long long)host,loaded->CodeBuffer.size());
 // Remove all dispatch entries while cache backing is alive, then destroy thread and code mapping.
 {FEXCore::Allocator::ScopedJITWriteProtect write;ctx->ClearCodeCache(t);}
 bool cleared=t->LookupCache->FindBlock(t,requested)==0;
 loaded.reset();
 const bool unregistered=!cache.IsAddressInMappedCodeBuffer(host);
 bytes[10]=9; // Modify an actual guest instruction after invalidation, not just its data.
 t->CurrentFrame->State.rip=requested;ctx->ExecuteThread(t);
 const bool modified=t->CurrentFrame->State.gregs[FEXCore::X86State::REG_RAX]==value+(wide?9:20);
 std::printf("CACHE stale_entry_after_clear=%d unregistered=%d changed_guest_recompiled=%d\n",!cleared,unregistered,modified);
 ctx->DestroyThread(t);ctx.reset();munmap(file,file_size);munmap(guest,page);
 return result&&cleared&&unregistered&&modified?0:15;
}
