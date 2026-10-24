#include "bb_cpu_cache_build.h"
// Isolated, bounded offline experiment. Decodes real game code; never executes it.
#include AETHER_ENGINE_SOURCE
#include "boot_image.h"
#include "startup_patches.h"
#include "bb_static_code_files.h"
#include "bb_cpu_cache_validation.h"
#include "bb_fex_eager_cache.h"
#include "git_version.h"
#include "Interface/Core/JIT/Relocations.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
namespace {
struct Handler final:FEXCore::HLE::SyscallHandler{
 BbStaticCode::Files& files;explicit Handler(BbStaticCode::Files& f):files(f){OSABI=FEXCore::HLE::SyscallOSABI::OS_GENERIC;}
 uint64_t HandleSyscall(FEXCore::Core::CpuStateFrame*,FEXCore::HLE::SyscallArguments*)override{std::abort();}
 FEXCore::HLE::ExecutableRangeInfo QueryGuestExecutableRange(FEXCore::Core::InternalThreadState*,uint64_t at)override{
  auto r=files.Query(at);return r?FEXCore::HLE::ExecutableRangeInfo{r->BeginVA,r->EndVA-r->BeginVA,false}:FEXCore::HLE::ExecutableRangeInfo{};
 }
 std::optional<FEXCore::ExecutableFileSectionInfo> LookupExecutableFileSection(FEXCore::Core::InternalThreadState*,uint64_t at)override{return files.Query(at);}
};
}
int main(int argc,char**argv){
 setvbuf(stdout,nullptr,_IONBF,0);
 try{
  const bool map_jit=argc==5&&std::string_view(argv[3])=="--validate-load-map-jit";
  const bool load_only=argc==5&&(std::string_view(argv[3])=="--validate-load"||map_jit);
  if(argc!=3&&!load_only)return 2;namespace fs=std::filesystem;
  const auto private_path=[](const fs::path& path){auto relative=fs::canonical(path).lexically_relative("/Users/Shared/BloodEchoesDevelopment");return !relative.empty()&&relative!="."&&*relative.begin()!="..";};
  BbStaticCode::Require(private_path(argv[1])&&private_path(fs::path(argv[2]).parent_path()),"offline paths must be private Shared inputs/output");
  BbBoot::Reader plan(argv[1]);char magic[8];plan.bytes(magic,8);uint32_t counts[4];plan.bytes(counts,sizeof(counts));
  const bool envelope_plan=!memcmp(magic,"BBAOT002",8);
  BbStaticCode::Require((envelope_plan||!memcmp(magic,"BBAOT001",8))&&counts[0]&&counts[0]<=131072&&counts[1]&&counts[1]<=256&&counts[2]&&counts[2]<=4096&&counts[3]<=4096,"invalid offline plan limits");
  BbCpuCache::Digest expected{};plan.bytes(expected.data(),expected.size());
  std::array<BbCpuCache::Digest,6> declared{};
  if(envelope_plan){
   BbStaticCode::Require(!load_only,"legacy no-execute loader requires raw-v1 plan");
   for(auto& digest:declared)plan.bytes(digest.data(),digest.size());
   BbStaticCode::Require(declared[0]==BbCacheBuild::Runtime&&declared[1]==BbCacheBuild::Bridge,"offline shared compiled build identity mismatch");
  }
  std::string boot_path(counts[2],'\0'),patch_path(counts[3],'\0');plan.bytes(boot_path.data(),boot_path.size());if(!patch_path.empty())plan.bytes(patch_path.data(),patch_path.size());
  BbStaticCode::Require(boot_path.find('\0')==std::string::npos&&patch_path.find('\0')==std::string::npos&&private_path(boot_path)&&(patch_path.empty()||private_path(patch_path)),"invalid offline source paths");
  std::vector<BbBoot::Segment> segments;for(unsigned i=0;i<counts[0];++i){auto lo=plan.word(),hi=plan.word();BbStaticCode::Require(lo<hi&&lo%4096==0&&hi<=512u<<20&&(segments.empty()||lo>=segments.back().address+segments.back().size),"offline section bounds");segments.push_back({lo,hi-lo,5});}
  std::vector<uint64_t> entries;for(unsigned i=0;i<counts[1];++i){auto at=plan.word();BbStaticCode::Require(entries.empty()||at>entries.back(),"offline entry order");entries.push_back(at);}
  BbStaticCode::Require(plan.f.peek()==EOF,"offline plan tail");
  BbBoot::Boot boot(boot_path);BbStaticCode::Require(boot.format=="BBPROBE5"&&boot.caps==1,"offline linked boot required");
  AetherPS4::Fex::Mapping image(boot.size,PROT_READ|PROT_WRITE);BbStaticCode::Require(image.IsValid(),"offline image allocation");boot.load(image.Get());
  if(!patch_path.empty())BbStartup::Apply(patch_path,image.Get(),boot);
  for(auto segment:segments)BbStaticCode::Require(boot.covered(segment.address,segment.size,true),"offline section outside source executable segment");
  boot.segments=segments;const uintptr_t base=reinterpret_cast<uintptr_t>(image.Get());
  BbStaticCode::Files files(boot,base,std::span(static_cast<const std::byte*>(image.Get()),size_t(boot.size)));files.Finalize(std::span(static_cast<const std::byte*>(image.Get()),size_t(boot.size)));
  BbStaticCode::Require(files.Identity()==expected,"offline final source identity mismatch");
  for(auto entry:entries)BbStaticCode::Require(entry<boot.size&&files.Query(base+entry).has_value(),"offline entry outside eligible sections");
  std::unique_ptr<AetherPS4::Fex::Mapping> cache_file;
  size_t cache_size=0;std::optional<BbCacheValidation::Validated> validated;
  if(load_only){
   BbStaticCode::Require(private_path(argv[2]),"cache input must be private Shared");
   int input=open(argv[2],O_RDONLY|O_NOFOLLOW);struct stat st{};
   BbStaticCode::Require(input>=0&&fstat(input,&st)==0&&S_ISREG(st.st_mode)&&st.st_size>=56&&st.st_size<=16u<<20,"cache input size/type");
   cache_size=st.st_size;cache_file=std::make_unique<AetherPS4::Fex::Mapping>(cache_size,PROT_READ|PROT_WRITE);
   BbStaticCode::Require(cache_file->IsValid(),"cache private snapshot allocation");
   size_t at=0;while(at<cache_size){auto n=pread(input,static_cast<std::byte*>(cache_file->Get())+at,cache_size-at,at);if(n<0&&errno==EINTR)continue;BbStaticCode::Require(n>0,"cache private snapshot read");at+=n;}close(input);
   auto data=std::span(static_cast<const std::byte*>(cache_file->Get()),cache_size);
   BbStaticCode::Require(BbStaticCode::Hex(BbCpuCache::Hash(data))==argv[4],"cache exact candidate SHA mismatch");
   std::vector<std::pair<uint64_t,uint64_t>> ranges;for(auto s:segments)ranges.emplace_back(s.address,s.address+s.size);
   validated=BbCacheValidation::Validate(data,{boot.size,ranges,GIT_HASH});
   BbStaticCode::Require(validated.has_value(),"cache structural validation before FEX");
   BbStaticCode::Require(mprotect(cache_file->Get(),cache_size,PROT_READ)==0,"cache snapshot must be immutable");
   std::printf("OFFLINE_GAME_CACHE validated_before_FEX=1 bytes=%zu blocks=%zu relocations=%zu\n",cache_size,validated->blocks.size(),validated->relocs.size());
  }
  auto lease=AetherPS4::Fex::FexConfigLease::Acquire();BbStaticCode::Require(!std::holds_alternative<AetherPS4::Fex::EngineFailure>(lease),"offline config lease");
  FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLECODECACHINGWIP,"1");FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLELAZYCODECACHINGWIP,map_jit?"1":"0");
  FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLECODECACHEVALIDATION,"0");
  Handler sys(files);AetherPS4::Fex::BridgeSignalDelegator signals(0);
  auto context=FEXCore::Context::Context::CreateNewContext(FEX::FetchHostFeatures());BbStaticCode::Require(bool(context),"offline FEX context");
  context->SetSyscallHandler(&sys);context->SetSignalDelegator(&signals);context->EnableExitOnHLT();if(!load_only)context->GetCodeCache().InitiateCacheGeneration();
  BbStaticCode::Require(context->InitCore(),"offline core init");
  BbCpuCache::Identity envelope_identity{};
  if(envelope_plan){
   auto* impl=static_cast<FEXCore::Context::ContextImpl*>(context.get());
   envelope_identity.runtime_build=BbCacheBuild::Runtime;envelope_identity.patched_code=files.Identity();
   envelope_identity.bridge_abi=declared[2];envelope_identity.module_set=declared[3];
   envelope_identity.host_features=BbCacheInputs::Features(impl->HostFeatures);envelope_identity.codegen_config=BbCacheInputs::ContextConfig(*impl);
   BbStaticCode::Require(impl->IsAtomicTSOEnabled()&&envelope_identity.host_features==declared[4]&&envelope_identity.codegen_config==declared[5]&&BbCpuCache::Valid(envelope_identity),"offline host/config/ordering identity mismatch");
  }
 AetherPS4::Fex::Mapping stack(16384,PROT_READ|PROT_WRITE);BbStaticCode::Require(stack.IsValid(),"offline stack");
  auto*t=context->CreateThread(base+entries.front(),reinterpret_cast<uintptr_t>(stack.Get())+16384-16);BbStaticCode::Require(t,"offline thread");
  AetherPS4::Fex::CallRetStack ret;BbStaticCode::Require(ret.IsReserved()&&!std::holds_alternative<AetherPS4::Fex::EngineFailure>(ret.MakeWritable()),"offline callret");ret.Initialize(t);
  AetherPS4::Fex::GuestSegmentState segment;segment.Initialize(t->CurrentFrame->State);
  if(load_only){
   auto& cache=context->GetCodeCache();auto section=files.Query(base+entries.front()).value();
   auto loaded=cache.LoadCache(std::span(static_cast<std::byte*>(cache_file->Get()),cache_size),section.FileInfo,base);
   BbStaticCode::Require(bool(loaded),"validated cache loader rejected");
   if(map_jit){
    auto* impl=static_cast<FEXCore::Context::ContextImpl*>(context.get());
    BbStaticCode::Require(BbEagerCache::Prepare(*impl,*loaded,*validated),"startup MAP_JIT cache preparation");
    std::printf("OFFLINE_GAME_CACHE startup_map_jit=1 all_pages_ready_before_publication=1\n");
   }
   cache.RegisterMappedCodeBuffer(*loaded);
   // Only sections containing blocks need publication. Eager loading finalizes the
   // complete ARM buffer once; avoid repeated finalization of already-loaded pages.
   bool finalized=false;
   for(auto span:segments){
    bool has=std::any_of(validated->blocks.begin(),validated->blocks.end(),[&](auto& b){return b.guest>=span.address&&b.guest<span.address+span.size;});
    if(!has)continue;
    BbStaticCode::Require(!finalized,"multi-section publication not yet supported by this isolated loader");
    auto selected=files.Query(base+span.address).value();BbStaticCode::Require(cache.EnableLoadedSection(t,*loaded,selected),"cache publication failed");finalized=true;
   }
   BbStaticCode::Require(finalized,"no cached section enabled");
   const auto host_base=reinterpret_cast<uintptr_t>(loaded->CodeBuffer.data());
   auto* impl=static_cast<FEXCore::Context::ContextImpl*>(context.get());const auto jit_before=impl->LatestOffset;
   for(auto& b:validated->blocks)BbStaticCode::Require(t->LookupCache->FindBlock(t,base+b.guest)==host_base+b.host,"loaded dispatch mismatch");
   for(auto r:validated->relocs){auto view=loaded->CodeBuffer.subspan(r.offset,r.kind==3?16:8);uint64_t value=0;
    if(r.kind==3){auto decoded=BbCacheValidation::DecodeMove(view,r.reg);BbStaticCode::Require(decoded.has_value(),"loaded ARM move encoding");value=*decoded;}
    else memcpy(&value,view.data(),8);
    const auto wanted=r.kind==0?FEXCore::CPU::GetNamedSymbolLiteral(*impl,FEXCore::CPU::RelocNamedSymbolLiteral::NamedSymbol::SYMBOL_LITERAL_EXITFUNCTION_LINKER):base+r.rip;
    BbStaticCode::Require(value==wanted,"loaded relocation value mismatch");
   }
   // A real game backpatches ordered unaligned accesses and dynamic links.
   // Probe the SAME write gate on a disposable child, catching its expected
   // protection fault. Never execute guest code or alter the parent's bytes.
   const auto probe=fork();BbStaticCode::Require(probe>=0,"cache write-gate probe fork");
   if(probe==0){
    struct sigaction action{};action.sa_handler=+[](int){_exit(86);};sigemptyset(&action.sa_mask);
    if(sigaction(SIGSEGV,&action,nullptr)||sigaction(SIGBUS,&action,nullptr))_exit(87);
    FEXCore::Allocator::ScopedJITWriteProtect write;
    auto* pointer=static_cast<volatile uint32_t*>(FEXCore::Allocator::GetWritableAddress(reinterpret_cast<void*>(host_base)));
    const auto original=*pointer;*pointer=original;_exit(0);
   }
   int probe_status=0;pid_t reaped;do{reaped=waitpid(probe,&probe_status,0);}while(reaped<0&&errno==EINTR);
   BbStaticCode::Require(reaped==probe&&WIFEXITED(probe_status)&&(WEXITSTATUS(probe_status)==0||WEXITSTATUS(probe_status)==86),"cache write-gate probe unexpected exit");
   std::printf("OFFLINE_GAME_CACHE write_gate_probe=1 live_backpatch_safe=%d expected_protection_fault=%d guest_executed=0\n",WEXITSTATUS(probe_status)==0,WEXITSTATUS(probe_status)==86);
   BbStaticCode::Require(jit_before==impl->LatestOffset,"unexpected loader JIT compilation");
   {FEXCore::Allocator::ScopedJITWriteProtect write;context->ClearCodeCache(t);}
   for(auto& b:validated->blocks)BbStaticCode::Require(t->LookupCache->FindBlock(t,base+b.guest)==0,"stale cache dispatch after clear");
   loaded.reset();BbStaticCode::Require(!cache.IsAddressInMappedCodeBuffer(host_base),"cache mapping registration retained");
   context->DestroyThread(t);context.reset();AetherPS4::Fex::FexConfigLease::Release();
   std::printf("OFFLINE_GAME_CACHE loaded_no_execute=1 relocated=%zu dispatch_entries=%zu cleared=1 unregistered=1 guest_executed=0 cache_loaded=1 guest_base=%#llx host_base=%#llx\n",validated->relocs.size(),validated->blocks.size(),(unsigned long long)base,(unsigned long long)host_base);return 0;
  }
  unsigned compiled=0,rejected=0;auto* impl=static_cast<FEXCore::Context::ContextImpl*>(context.get());auto buffer=impl->GetLatest();
  for(auto entry:entries){
   FEXCore::Allocator::ScopedJITWriteProtect write;
   if(!context->CheckIfBlockIsCacheable(*t,base+entry,FEXCore::Config::Get_MAXINST()())){++rejected;continue;}
   context->CompileRIP(t,base+entry);BbStaticCode::Require(buffer==impl->GetLatest(),"offline batch overflowed code buffer");
   if(t->LookupCache->FindBlock(t,base+entry))++compiled;else ++rejected;
  }
  BbStaticCode::Require(compiled!=0,"offline no blocks compiled");
  for(const auto&[guest,block]:t->LookupCache->Shared->BlockList){BbStaticCode::Require(files.Query(guest).has_value(),"offline compiled entry escaped sections");for(auto page:block.CodePages)BbStaticCode::Require(files.Query(page).has_value(),"offline decoded page escaped sections");}
  const fs::path raw_output=envelope_plan?std::string(argv[2])+".raw.tmp":argv[2];
  int fd=open(raw_output.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);BbStaticCode::Require(fd>=0,"offline output must be new");
  auto section=files.Query(base+entries.front()).value();bool saved;
  {FEXCore::Allocator::ScopedJITWriteProtect write;saved=context->GetCodeCache().SaveData(*t,fd,section,0);}
  const bool durable=saved&&fsync(fd)==0;struct stat st{};const bool regular=fstat(fd,&st)==0&&S_ISREG(st.st_mode);close(fd);
  const auto code_bytes=impl->LatestOffset;context->DestroyThread(t);buffer.reset();context.reset();AetherPS4::Fex::FexConfigLease::Release();
  BbStaticCode::Require(durable&&regular,"offline serialization failed");
  if(envelope_plan){
   BbStaticCode::Require(st.st_size>0&&st.st_size<=1024*1024,"offline envelope exceeds experimental consumption budget");
   int input=open(raw_output.c_str(),O_RDONLY|O_NOFOLLOW);BbStaticCode::Require(input>=0,"offline raw verification open");
   void* raw=mmap(nullptr,st.st_size,PROT_READ,MAP_PRIVATE,input,0);close(input);BbStaticCode::Require(raw!=MAP_FAILED,"offline raw verification mapping");
   auto data=std::span(static_cast<const std::byte*>(raw),size_t(st.st_size));std::vector<std::pair<uint64_t,uint64_t>> spans;for(auto s:segments)spans.emplace_back(s.address,s.address+s.size);
   BbStaticCode::Require(BbCacheValidation::Validate(data,{boot.size,spans,GIT_HASH}).has_value(),"offline raw validation before envelope");
   const auto prefix=BbCpuCache::Prefix(data,envelope_identity);const fs::path temporary=std::string(argv[2])+".envelope.new";
   int output=open(temporary.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);BbStaticCode::Require(output>=0,"offline envelope must be new");
   const auto write_all=[&](std::span<const std::byte> bytes){while(!bytes.empty()){auto n=write(output,bytes.data(),bytes.size());if(n<0&&errno==EINTR)continue;if(n<=0)return false;bytes=bytes.subspan(n);}return true;};
   const bool complete=write_all(prefix)&&write_all(data)&&fsync(output)==0;close(output);munmap(raw,st.st_size);
   BbStaticCode::Require(complete,"offline envelope write failed");
   // Publish an entirely written new inode, without ever replacing an existing
   // candidate. Crash-durable directory publication remains a separate gate.
   BbStaticCode::Require(link(temporary.c_str(),argv[2])==0,"offline final candidate already exists or publish failed");
   unlink(temporary.c_str());unlink(raw_output.c_str());
   std::printf("OFFLINE_GAME_CACHE envelope=1 payload_validated=1 runtime=%s key=%s guest_executed=0 cache_loaded=0\n",BbStaticCode::Hex(envelope_identity.runtime_build).c_str(),BbStaticCode::Hex(BbCpuCache::Key(envelope_identity)).c_str());
  }
  std::printf("OFFLINE_GAME_CACHE requested=%zu compiled=%u rejected=%u code_bytes=%zu file_bytes=%lld source_identity=%s guest_executed=0 cache_loaded=0\n",entries.size(),compiled,rejected,size_t(code_bytes),(long long)st.st_size,BbStaticCode::Hex(files.Identity()).c_str());return 0;
 }catch(const std::exception& error){std::fprintf(stderr,"OFFLINE_GAME_CACHE STOP %s\n",error.what());return 1;}
}
