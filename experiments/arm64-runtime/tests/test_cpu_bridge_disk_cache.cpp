// Includes the actual embedding implementation to exercise its private syscall handler.
// No forked handler copy, no production API addition, and no game linkage changes.
#include AETHER_ENGINE_SOURCE
#include "core/guest_cpu/fex_hle_bridge.h"
#include "core/guest_cpu/fex_guest_cpu.h"
#include <FEXCore/HLE/SourcecodeResolver.h>
#include <CommonCrypto/CommonDigest.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <set>
#include "cpu_cache_fixture_validation.h"
#include "cpu_cache_identity_inputs.h"
#ifdef BB_CACHE_MAP_JIT
#include "bb_fex_eager_cache.h"
#include "git_version.h"
#endif
using namespace Core::GuestCpu;
#ifdef BB_CACHE_ENGINE_CONSUMER
constexpr size_t SecondOffset=0x2000;
#else
constexpr size_t SecondOffset=0x200;
#endif
static bool validate_authored(std::span<const std::byte> bytes){
#ifdef BB_CACHE_ENGINE_CONSUMER
 std::array<std::pair<uint64_t,uint64_t>,2> spans{{{0,4096},{8192,12288}}};
 return BbCacheValidation::Validate(bytes,{16384,spans,GIT_HASH}).has_value();
#else
 return validate(bytes);
#endif
}
struct CachePair {uint64_t a,b;};
namespace Core::GuestCpu {
template<>struct GuestAggregate<CachePair>{static constexpr bool enabled=true;
 static constexpr auto fields=std::tuple{GuestField<uint64_t,0>{},GuestField<uint64_t,8>{}};};
}
static unsigned seven_calls,pair_calls;
#ifdef BB_CACHE_MAP_JIT
static uint64_t unaligned_address;static unsigned pointer_calls;
static uint64_t pointer_result(){++pointer_calls;return unaligned_address;}
static volatile sig_atomic_t cached_alignment_fixes=0;
static void cached_fault(int sig,siginfo_t* info,void* context){
 if(AetherPS4::Fex::HandleGuestSignal(sig,info,context)){cached_alignment_fixes=cached_alignment_fixes+1;return;}
 _exit(128+sig);
}
static bool install_cached_faults(){
 for(int sig:{SIGBUS,SIGSEGV}){struct sigaction action{};action.sa_sigaction=cached_fault;action.sa_flags=SA_SIGINFO|SA_ONSTACK;sigemptyset(&action.sa_mask);if(sigaction(sig,&action,nullptr))return false;}return true;
}
#endif
static uint64_t seven(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g){++seven_calls;return a+b+c+d+e+f+g;}
static uint64_t pair_sum(CachePair p){++pair_calls;return p.a+p.b;}
static bool validate_range(void* context,uintptr_t a,size_t n,bool){
 auto* r=static_cast<Core::GuestExecutionRange*>(context);
 return n&&a>=r->Begin&&a-r->Begin<r->Size&&n<=r->Size-(a-r->Begin);
}
int main(int argc,char**argv){
 setvbuf(stdout,nullptr,_IONBF,0);if(argc!=5)return 2;
#ifdef BB_CACHE_MAP_JIT
 if(!install_cached_faults())return 44;
#endif
 using Handler=AetherPS4::Fex::BridgeSyscallHandler;
 using Signals=AetherPS4::Fex::BridgeSignalDelegator;
 const bool bad_fd=std::string_view(argv[1])=="generate-bad-fd";
 const bool limited=std::string_view(argv[1])=="generate-limited-fd",offset_fd=std::string_view(argv[1])=="generate-offset-fd",pipe_fd=std::string_view(argv[1])=="generate-pipe-fd";
 const bool generate=std::string_view(argv[1])=="generate"||bad_fd||limited||offset_fd||pipe_fd;
 const std::string_view mode=argv[1];if(!generate&&!mode.starts_with("load"))return 2;
 int fd=-1;void* mapped=MAP_FAILED;size_t length=0;
 if(!generate){
  fd=open(argv[2],O_RDONLY|O_NOFOLLOW);struct stat st{};
  if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<56||st.st_size>1024*1024+BbCpuCache::PrefixBytes)return 20;
  length=st.st_size;mapped=mmap(nullptr,length,PROT_READ,MAP_PRIVATE,fd,0);close(fd);
  if(mapped==MAP_FAILED)return 20;
  std::span<const std::byte> bytes{static_cast<const std::byte*>(mapped),length};
  if(!digest_ok(bytes,argv[4]))return 21;
 }
 auto lease=AetherPS4::Fex::FexConfigLease::Acquire();if(std::holds_alternative<AetherPS4::Fex::EngineFailure>(lease))return 3;
 #ifdef BB_CACHE_NORMAL_CONSUMER
 FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLECODECACHINGWIP,generate?"1":"0");
#else
 FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLECODECACHINGWIP,"1");
#endif
 #if defined(BB_CACHE_MAP_JIT) && !defined(BB_CACHE_NORMAL_CONSUMER)
 FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLELAZYCODECACHINGWIP,"1");
#else
 FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLELAZYCODECACHINGWIP,"0");
#endif
 FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLECODECACHEVALIDATION,"0");
 if(mode=="load-changed-config")FEXCore::Config::Set(FEXCore::Config::CONFIG_SMALLTSCSCALE,"1");
 auto ctx=FEXCore::Context::Context::CreateNewContext(FEX::FetchHostFeatures());if(!ctx)return 4;
 const size_t page=sysconf(_SC_PAGESIZE);if(page!=16384)return 5;
 const auto requested=static_cast<uintptr_t>(std::strtoull(argv[3],nullptr,0));
 void* memory=mmap(reinterpret_cast<void*>(requested),page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 if(memory==MAP_FAILED||reinterpret_cast<uintptr_t>(memory)!=requested)return 6;
 AetherPS4::Fex::Mapping stack(page,PROT_READ|PROT_WRITE);if(!stack.IsValid())return 7;
 Core::GuestExecutionRange stack_range{reinterpret_cast<uintptr_t>(stack.Get()),page,false,true};
 HleCallRegistry registry;
 if(mode=="load-reordered")registry.Register(MakeHleCallAdapter(+[]()->uint64_t{return 99;}),"cache.fixture.extra");
 auto add=registry.Register(MakeHleCallAdapter(seven),"cache.fixture.seven");
 auto pair=registry.Register(MakeHleCallAdapter(pair_sum),"cache.fixture.pair");
#ifdef BB_CACHE_MAP_JIT
 auto pointer=registry.Register(MakeHleCallAdapter(pointer_result),"cache.fixture.pointer");
 unaligned_address=requested+0x380f;
#endif
 HleGuestBridge bridge(registry,validate_range,&stack_range);
 Signals signals(0);auto sys=fextl::make_unique<Handler>(bridge);
 ctx->SetSignalDelegator(&signals);ctx->SetSyscallHandler(sys.get());ctx->EnableExitOnHLT();
 if(generate)ctx->GetCodeCache().InitiateCacheGeneration();if(!ctx->InitCore())return 8;
 auto* bytes=static_cast<unsigned char*>(memory);std::memset(bytes,0xf4,page);
 std::vector<unsigned char> first={0xbf,1,0,0,0,0xbe,2,0,0,0,0xba,3,0,0,0,
  0x41,0xba,4,0,0,0,0x41,0xb8,5,0,0,0,0x41,0xb9,6,0,0,0,0x48,0xb8};
 auto append64=[](auto& b,uint64_t x){const auto n=b.size();b.resize(n+8);std::memcpy(b.data()+n,&x,8);};
 append64(first,add->Operation());first.insert(first.end(),{0x0f,0x05});const size_t first_cont=first.size();
 first.insert(first.end(),{0x48,0x83,0xc0,10,0xf4});std::memcpy(bytes,first.data(),first.size());
 std::vector<unsigned char> second={0xbf,11,0,0,0,0xbe,13,0,0,0,0x48,0xb8};
 append64(second,pair->Operation());second.insert(second.end(),{0x0f,0x05});const size_t second_cont=SecondOffset+second.size();
 second.insert(second.end(),{0x48,0x83,0xc0,20,0xf4});std::memcpy(bytes+SecondOffset,second.data(),second.size());
#ifdef BB_CACHE_MAP_JIT
 // The real HLE bridge returns a dynamic unaligned address. Keeping this out
 // of RIP-relative constant folding exercises the game's ordered-load fixup.
 std::vector<unsigned char> third={0x48,0xb8};append64(third,pointer->Operation());third.insert(third.end(),{0x0f,0x05});
 const size_t third_cont=0x400+third.size();third.insert(third.end(),{0x48,0x8b,0x00,0xf4});
 memcpy(bytes+0x400,third.data(),third.size());const uint64_t unaligned_value=0x123456789abcdef0ULL;memcpy(bytes+0x380f,&unaligned_value,8);
 const unsigned char jump[]={0xe9,0xfb,0,0,0};memcpy(bytes+0x600,jump,sizeof(jump));
 const unsigned char target[]={0xb8,0x55,0,0,0,0xf4};memcpy(bytes+0x700,target,sizeof(target));
#endif
 if(mode=="load-changed-patch")bytes[first_cont+3]=12;
 BbCpuCache::Identity identity{};identity.runtime_build=CacheFixture::Build(argv[0]);
 identity.patched_code=BbCpuCache::Hash({reinterpret_cast<const std::byte*>(memory),
#ifdef BB_CACHE_ENGINE_CONSUMER
 page
#else
 4096
#endif
 });
 identity.module_set=CacheFixture::Text("authored-main-module-v1/no-other-modules");
 identity.bridge_abi=CacheFixture::Text("seven:u64(u64*7):"+std::to_string(add->Operation())+";pair:u64({u64,u64}):"+std::to_string(pair->Operation()));
#ifdef BB_CACHE_MAP_JIT
 identity.bridge_abi=CacheFixture::Text("seven:u64(u64*7):"+std::to_string(add->Operation())+";pair:u64({u64,u64}):"+std::to_string(pair->Operation())+";pointer:u64():"+std::to_string(pointer->Operation()));
#endif
 identity.host_features=CacheFixture::Features(static_cast<FEXCore::Context::ContextImpl*>(ctx.get())->HostFeatures);
 #ifdef BB_CACHE_NORMAL_CONSUMER
 #ifdef BB_CACHE_ENGINE_CONSUMER
 identity.codegen_config=CacheFixture::ContextConfig(*static_cast<FEXCore::Context::ContextImpl*>(ctx.get()));
#else
 identity.codegen_config=CacheFixture::CanonicalConfig();
#endif
 if(!generate)static_cast<FEXCore::Context::ContextImpl*>(ctx.get())->GetCodeCache().ExternalStartupConsumption=true;
#else
 identity.codegen_config=CacheFixture::Config();
#endif
 if(mode=="load-wrong-runtime-key")identity.runtime_build[0]^=0x80;
 if(mode=="load-hardware-key")identity.ordering=BbCpuCache::Identity::Ordering::Hardware;
 if(!BbCpuCache::Valid(identity))return 28;
 std::span<const std::byte> payload;
 if(!generate){
   payload=BbCpuCache::Check({static_cast<const std::byte*>(mapped),length},identity);
   if(payload.empty()||!validate_authored(payload)){
     std::printf("BRIDGE_CACHE identity_or_structure_rejected_before_FEX_load=1 seven_calls=%u pair_calls=%u\n",seven_calls,pair_calls);
     ctx.reset();munmap(mapped,length);munmap(memory,page);AetherPS4::Fex::FexConfigLease::Release();return 30;
   }
 }
 const uint64_t stack_argument=7;std::memcpy(static_cast<unsigned char*>(stack.Get())+page-8,&stack_argument,8);
 FEXCore::ExecutableFileInfo info{};info.FileId=0x789;info.Filename="authored-actual-hle-bridge";
 FEXCore::ExecutableFileSectionInfo section{info,requested,requested,requested+4096};
 const uint64_t rsp=reinterpret_cast<uintptr_t>(stack.Get())+page-16;
#ifdef BB_CACHE_NORMAL_CONSUMER
 const auto initial_bytes=static_cast<FEXCore::Context::ContextImpl*>(ctx.get())->GetLatest()->AllocatedSize;
 std::printf("BRIDGE_CACHE initial_jit_bytes=%zu consumer=%d\n",initial_bytes,!generate);
 if(!generate&&initial_bytes!=16*1024*1024)return 50;
#endif
 auto*t=ctx->CreateThread(requested,rsp);if(!t)return 9;
 AetherPS4::Fex::CallRetStack cr;if(!cr.IsReserved()||std::holds_alternative<AetherPS4::Fex::EngineFailure>(cr.MakeWritable()))return 10;
 cr.Initialize(t);AetherPS4::Fex::GuestSegmentState segments;segments.Initialize(t->CurrentFrame->State);
 std::vector<Core::GuestExecutionRange> ranges{{requested,page,true,false}};
 if(std::holds_alternative<AetherPS4::Fex::EngineFailure>(sys->RegisterThread(t,ranges)))return 11;
 auto destroy=[&]{sys->UnregisterThread(t);ctx->DestroyThread(t);ctx.reset();if(mapped!=MAP_FAILED)munmap(mapped,length);munmap(memory,page);AetherPS4::Fex::FexConfigLease::Release();};
 if(generate){
  {FEXCore::Allocator::ScopedJITWriteProtect write;for(size_t off:{size_t(0),first_cont,size_t(SecondOffset),second_cont})ctx->CompileRIP(t,requested+off);
#ifdef BB_CACHE_MAP_JIT
   ctx->CompileRIP(t,requested+0x400);ctx->CompileRIP(t,requested+third_cont);
   ctx->CompileRIPCount(t,requested+0x600,1);
   if(t->LookupCache->FindBlock(t,requested+0x700)!=0)return 49;
#endif
  }
  fd=open(argv[2],O_CREAT|O_EXCL|O_WRONLY,0600);if(fd<0)return 12;
  if(bad_fd||limited||offset_fd||pipe_fd){
    int reader=-1;
    if(bad_fd){close(fd);fd=open(argv[2],O_RDONLY|O_NOFOLLOW);if(fd<0)return 12;}
    if(limited){struct rlimit cap{1024,1024};if(setrlimit(RLIMIT_FSIZE,&cap))return 12;std::signal(SIGXFSZ,SIG_IGN);}
    if(offset_fd){const char byte='X';if(write(fd,&byte,1)!=1)return 12;}
    if(pipe_fd){close(fd);int ends[2];if(pipe(ends))return 12;reader=ends[0];fd=ends[1];}
    bool returned;{FEXCore::Allocator::ScopedJITWriteProtect write;returned=ctx->GetCodeCache().SaveData(*t,fd,section,0);}
    close(fd);if(reader>=0)close(reader);std::printf("BRIDGE_CACHE fault_mode=%s save_reported_success=%d\n",argv[1],returned);destroy();return returned?41:0;
  }
  bool ok;{FEXCore::Allocator::ScopedJITWriteProtect write;ok=ctx->GetCodeCache().SaveData(*t,fd,section,0);}
  ok=ok&&fsync(fd)==0;close(fd);
  // Wrap only this process's newly created authored payload, then replace it atomically.
  fd=open(argv[2],O_RDONLY|O_NOFOLLOW);struct stat st{};if(fd<0||fstat(fd,&st)||st.st_size<56||st.st_size>1024*1024)return 31;
  void* raw=mmap(nullptr,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);if(raw==MAP_FAILED)return 31;
  std::span<const std::byte> content{static_cast<const std::byte*>(raw),size_t(st.st_size)};
  ok=ok&&validate_authored(content);auto prefix=BbCpuCache::Prefix(content,identity);const std::string outer=std::string(argv[2])+".envelope.new";
  int wrapped=open(outer.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);if(wrapped<0)return 31;
  auto write_all=[&](std::span<const std::byte> source){while(!source.empty()){auto count=write(wrapped,source.data(),source.size());if(count<0&&errno==EINTR)continue;if(count<=0)return false;source=source.subspan(count);}return true;};
  ok=ok&&write_all(prefix)&&write_all(content)&&fsync(wrapped)==0;close(wrapped);munmap(raw,st.st_size);
  if(ok)ok=rename(outer.c_str(),argv[2])==0;
  std::printf("BRIDGE_CACHE generated=%d seven_operation=%llu pair_operation=%llu host_fn=%p guest=%#llx\n",ok,(unsigned long long)add->Operation(),(unsigned long long)pair->Operation(),reinterpret_cast<void*>(seven),(unsigned long long)requested);
  destroy();return ok?0:13;
 }
#ifdef BB_CACHE_ENGINE_CONSUMER
 {
  if(mprotect(memory,page,PROT_READ))return 66;
  auto created=Core::FexGuestCpuBackend::Create(bridge);if(!std::holds_alternative<std::unique_ptr<Core::FexGuestCpuBackend>>(created))return 51;
  auto engine=std::move(std::get<std::unique_ptr<Core::FexGuestCpuBackend>>(created));
  const uint64_t reserve_bytes=getenv("BB_TEST_JIT_RESERVE_128")?128*1024*1024:16*1024*1024;
  if(!std::holds_alternative<Core::GuestExecutionFailure>(engine->ConfigureStartupJitReserve(96*1024*1024)))return 69;
  auto configured=engine->ConfigureStartupJitReserve(reserve_bytes);
  if(!std::holds_alternative<uint64_t>(configured)||std::get<uint64_t>(configured)!=reserve_bytes)return 70;
  if(reserve_bytes>16*1024*1024&&!std::holds_alternative<Core::GuestExecutionFailure>(engine->ConfigureStartupJitReserve(16*1024*1024)))return 71;
  const auto env_result=engine->CacheEnvironment();if(!std::holds_alternative<AetherPS4::Fex::CpuCacheEnvironment>(env_result))return 52;
  const auto environment=std::get<AetherPS4::Fex::CpuCacheEnvironment>(env_result);
  if(!environment.software_tso||environment.host_features!=identity.host_features||environment.codegen_config!=identity.codegen_config)return 53;
  std::array<std::pair<uint64_t,uint64_t>,2> eligible{{{0,4096},{8192,12288}}};
  const auto full=std::span(static_cast<const std::byte*>(mapped),length);
  const auto live_before=AetherPS4::Fex::Mapping::CurrentlyLive.load();unsigned rejects=0;
  const auto install=[&](std::span<const std::byte> file,const BbCpuCache::Identity& expected){return engine->InstallStartupCodeCache(file,expected,eligible,requested,page,info);};
  for(unsigned i=0;i<6;++i){auto wrong=identity;std::array<BbCpuCache::Digest*,6> fields{&wrong.runtime_build,&wrong.patched_code,&wrong.module_set,&wrong.bridge_abi,&wrong.host_features,&wrong.codegen_config};(*fields[i])[0]^=0x80;
   if(!std::holds_alternative<Core::GuestExecutionFailure>(install(full,wrong)))return 54;++rejects;
   if(AetherPS4::Fex::Mapping::CurrentlyLive.load()!=live_before)return 55;
  }
  auto damaged=std::vector<std::byte>(full.begin(),full.end());damaged.back()^=std::byte{0x80};
  if(!std::holds_alternative<Core::GuestExecutionFailure>(install(damaged,identity)))return 56;++rejects;
  std::vector<std::byte> oversized(BbCpuCache::PrefixBytes+1024*1024+1);
  if(!std::holds_alternative<Core::GuestExecutionFailure>(install(oversized,identity)))return 57;++rejects;
  auto installed=install(full,identity);if(!std::holds_alternative<AetherPS4::Fex::StartupCpuCacheInfo>(installed))return 58;
  const auto details=std::get<AetherPS4::Fex::StartupCpuCacheInfo>(installed);if(details.initial_jit_bytes!=reserve_bytes||!details.blocks)return 59;
  if(!std::holds_alternative<Core::GuestExecutionFailure>(install(full,identity)))return 60;++rejects;
  auto usage=[&](){auto result=engine->InspectStartupCodeCache();if(!std::holds_alternative<AetherPS4::Fex::StartupCpuCacheInfo>(result))std::abort();return std::get<AetherPS4::Fex::StartupCpuCacheInfo>(result);};
  const auto request=[&](size_t off){Core::GuestExecutionRequest r{};r.Rip=requested+off;r.Rsp=rsp;r.Rflags=2;r.MappedRanges={{requested,page,true,false},stack_range};return r;};
  const auto execute_api=[&](size_t off,uint64_t expected){auto result=engine->Run(request(off));return std::holds_alternative<Core::GuestExecutionState>(result)&&std::get<Core::GuestExecutionState>(result).Gpr[FEXCore::X86State::REG_RAX]==expected;};
  const auto before_api=usage();const bool correct=execute_api(0,38)&&execute_api(SecondOffset,44)&&execute_api(0x400,unaligned_value);
  const auto after_api=usage();const bool no_jit=before_api.jit_compiled_bytes==after_api.jit_compiled_bytes&&after_api.cached_resolutions>0;
  const bool first_link=execute_api(0x600,0x55);const auto first_link_usage=usage();
  const bool repeat=execute_api(0x600,0x55)&&usage().jit_compiled_bytes==first_link_usage.jit_compiled_bytes;
  if(mprotect(memory,page,PROT_READ|PROT_WRITE))return 67;
  bytes[0x701]=0x66;if(mprotect(memory,page,PROT_READ))return 68;
  auto pending=engine->CreateThread(request(0x600));if(!std::holds_alternative<std::unique_ptr<Core::FexGuestCpuBackend::Thread>>(pending))return 61;
  auto thread=std::move(std::get<std::unique_ptr<Core::FexGuestCpuBackend::Thread>>(pending));
  const bool busy_inspect=std::holds_alternative<Core::GuestExecutionFailure>(engine->InspectStartupCodeCache());
  auto invalidated=engine->Invalidate(*thread,requested,page);if(!std::holds_alternative<bool>(invalidated))return 62;
  auto rerun=engine->Run(*thread);const bool updated=std::holds_alternative<Core::GuestExecutionState>(rerun)&&std::get<Core::GuestExecutionState>(rerun).Gpr[FEXCore::X86State::REG_RAX]==0x66;
  if(!std::holds_alternative<bool>(engine->DestroyThread(thread)))return 63;
  if(!std::holds_alternative<Core::GuestExecutionFailure>(install(full,identity)))return 64;++rejects;
  const bool normal=usage().current_jit_bytes==reserve_bytes;
  engine.reset();const bool released=AetherPS4::Fex::Mapping::CurrentlyLive.load()==live_before-2;
  std::printf("BRIDGE_CACHE correct=%d no_fallback_jit=%d seven_calls=%u pair_calls=%u host_fn=%p guest=%#llx\n",correct,no_jit,seven_calls,pair_calls,reinterpret_cast<void*>(seven),(unsigned long long)requested);
  std::printf("BRIDGE_CACHE engine_startup_api=1 initial_jit_bytes=%llu consumer=1 bounded_rejects=%u invalidation_new_target=%d busy_inspect_rejected=%d linked_reuse=%d released=%d normal_jit=%d\n",(unsigned long long)details.initial_jit_bytes,rejects,updated,busy_inspect,first_link&&first_link_usage.jit_compiled_bytes>after_api.jit_compiled_bytes&&repeat,released,normal);
  std::printf("BRIDGE_CACHE pointer_calls=%u unaligned_dynamic_load_correct=%d cached_alignment_fixes=%d\n",pointer_calls,correct,int(cached_alignment_fixes));
  destroy();return correct&&no_jit&&seven_calls==1&&pair_calls==1&&pointer_calls==1&&cached_alignment_fixes>0&&updated&&busy_inspect&&repeat&&released&&normal&&rejects==10?0:65;
 }
#endif
 auto& cache=ctx->GetCodeCache();auto loaded=cache.LoadCache({const_cast<std::byte*>(payload.data()),payload.size()},info,requested);
 if(!loaded){destroy();return 14;}
#ifdef BB_CACHE_MAP_JIT
 std::array<std::pair<uint64_t,uint64_t>,1> eligible{{{0,4096}}};
 auto checked=BbCacheValidation::Validate(payload,{page,eligible,GIT_HASH});
 if(!checked)return 42;auto& cache_context=*static_cast<FEXCore::Context::ContextImpl*>(ctx.get());
 auto wrong=*checked;wrong.code_bytes+=4096;if(BbEagerCache::Prepare(cache_context,*loaded,wrong))return 45;
 wrong=*checked;wrong.code_offset+=8;if(BbEagerCache::Prepare(cache_context,*loaded,wrong))return 46;
 wrong=*checked;wrong.relocation_offset=wrong.code_offset+8;if(BbEagerCache::Prepare(cache_context,*loaded,wrong))return 47;
 auto* owner=loaded->CacheManager;loaded->CacheManager=nullptr;
 if(BbEagerCache::Prepare(cache_context,*loaded,*checked))return 48;loaded->CacheManager=owner;
 if(!BbEagerCache::Prepare(cache_context,*loaded,*checked))return 42;
 if(BbEagerCache::Prepare(cache_context,*loaded,*checked))return 43;
 std::printf("BRIDGE_CACHE map_jit_prepare_guard_checks=5 passed=1\n");
#endif
 cache.RegisterMappedCodeBuffer(*loaded);
 if(!cache.EnableLoadedSection(t,*loaded,section))return 15;
 auto* impl=static_cast<FEXCore::Context::ContextImpl*>(ctx.get());const auto offset_before=impl->LatestOffset;
 auto execute=[&](size_t off,uint64_t expected){
  const auto host=t->LookupCache->FindBlock(t,requested+off);
  if(!cache.IsAddressInMappedCodeBuffer(host))return false;
  Handler::InvocationState call;
  Handler::InvocationScope active(*sys,call,t);
  AetherPS4::Fex::FexExecutionSignalScope scope(*ctx,t,&signals,sys.get());
  t->CurrentFrame->State.rip=requested+off;t->CurrentFrame->State.gregs[FEXCore::X86State::REG_RSP]=rsp;
  ctx->ExecuteThread(t);
  return !sys->FailureResult(call)&&sys->Invoked(call)&&t->CurrentFrame->State.gregs[FEXCore::X86State::REG_RAX]==expected;
 };
 bool correct=execute(0,38)&&execute(SecondOffset,44);
#ifdef BB_CACHE_MAP_JIT
 correct=correct&&execute(0x400,unaligned_value);
#endif
 bool no_jit=offset_before==impl->LatestOffset;
 std::printf("BRIDGE_CACHE correct=%d no_fallback_jit=%d seven_calls=%u pair_calls=%u host_fn=%p guest=%#llx\n",correct,no_jit,seven_calls,pair_calls,reinterpret_cast<void*>(seven),(unsigned long long)requested);
#ifdef BB_CACHE_MAP_JIT
 // Cold direct-branch target is deliberately not in the disk cache. The first
 // jump must JIT it and patch the cached source; the second reuses that link.
 auto code_before=std::vector<std::byte>(loaded->CodeBuffer.begin(),loaded->CodeBuffer.end());
 auto run_link=[&](uint64_t expected){
  Handler::InvocationState call;Handler::InvocationScope active(*sys,call,t);
  AetherPS4::Fex::FexExecutionSignalScope scope(*ctx,t,&signals,sys.get());
  t->CurrentFrame->State.rip=requested+0x600;t->CurrentFrame->State.gregs[FEXCore::X86State::REG_RSP]=rsp;
  ctx->ExecuteThread(t);return !sys->FailureResult(call)&&t->CurrentFrame->State.gregs[FEXCore::X86State::REG_RAX]==expected;
 };
 const auto offset_link_before=impl->LatestOffset;
 const bool first_link=run_link(0x55);const auto after_first_link=impl->LatestOffset;
 const bool patched=!std::equal(code_before.begin(),code_before.end(),loaded->CodeBuffer.begin());
 const bool second_link=run_link(0x55)&&after_first_link==impl->LatestOffset;
 const bool linked=first_link&&after_first_link>offset_link_before&&patched&&second_link;
#endif
 {FEXCore::Allocator::ScopedJITWriteProtect write;ctx->ClearCodeCache(t);}
#ifdef BB_CACHE_MAP_JIT
 const bool cleared_links=t->LookupCache->FindBlock(t,requested+0x600)==0&&t->LookupCache->FindBlock(t,requested+0x700)==0;
 std::printf("BRIDGE_CACHE linked_new_target=%d cache_source_patched=%d stable_second_dispatch=%d cleared_links=%d\n",first_link&&after_first_link>offset_link_before,patched,second_link,cleared_links);
 // Re-publish the retained immutable source after clear, change only the
 // uncached guest target, then demand its NEW result. This catches a stale
 // cached direct branch even if all lookup entries had been removed correctly.
 bytes[0x701]=0x66;const auto offset_after_clear=impl->LatestOffset;
 const bool republished=cache.EnableLoadedSection(t,*loaded,section);
 const bool changed_target=republished&&run_link(0x66)&&impl->LatestOffset>offset_after_clear;
 {FEXCore::Allocator::ScopedJITWriteProtect write;ctx->ClearCodeCache(t);}
 const bool second_clear=t->LookupCache->FindBlock(t,requested+0x600)==0&&t->LookupCache->FindBlock(t,requested+0x700)==0;
 std::printf("BRIDGE_CACHE cache_republished=%d changed_target_recompiled=%d second_clear=%d\n",republished,changed_target,second_clear);
 correct=correct&&linked&&cleared_links&&changed_target&&second_clear;
#endif
 loaded.reset();destroy();
#ifdef BB_CACHE_MAP_JIT
 std::printf("BRIDGE_CACHE pointer_calls=%u unaligned_dynamic_load_correct=%d cached_alignment_fixes=%d\n",pointer_calls,correct,int(cached_alignment_fixes));
 return correct&&no_jit&&seven_calls==1&&pair_calls==1&&pointer_calls==1&&cached_alignment_fixes>0?0:16;
#else
 return correct&&no_jit&&seven_calls==1&&pair_calls==1?0:16;
#endif
}
