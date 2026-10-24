#define main upstream_smoke_main
#include AETHER_SMOKE_SOURCE
#undef main
#include "Interface/Core/LookupCache.h"
#include "Interface/Core/ArchHelpers/Arm64Emitter.h"
struct RegisterInspector: FEXCore::CPU::Arm64Emitter {using Arm64Emitter::Arm64Emitter;bool valid(){for(auto r:GeneralRegisters)if(r.Idx()==18)return false;for(auto r:StaticRegisters)if(r.Idx()==18)return false;for(auto r:GeneralRegistersNotPreserved)if(r.Idx()==18)return false;return true;}};
#include <mach/mach.h>
#include <mach/task_info.h>
#include <sys/resource.h>
#include <chrono>
static void memory(const char*phase){task_vm_info_data_t v{};mach_msg_type_number_t n=TASK_VM_INFO_COUNT;auto k=task_info(mach_task_self(),TASK_VM_INFO,(task_info_t)&v,&n);struct rusage r{};getrusage(RUSAGE_SELF,&r);std::printf("MEM phase=%s status=%d footprint=%llu resident=%llu virtual=%llu peak_rss=%ld\n",phase,k,(unsigned long long)v.phys_footprint,(unsigned long long)v.resident_size,(unsigned long long)v.virtual_size,r.ru_maxrss);}
static uint64_t reference(uint64_t x,uint64_t n){while(n--){x=x*1664525+1013904223;x=(x>>13)|(x<<51);}return x;}
int main(int argc,char**argv){
 const bool lookup=argc>1&&std::string_view(argv[1])=="lookup";
 memory("before_config");ConfigScope config;auto features=FEX::FetchHostFeatures();auto ctx=FEXCore::Context::Context::CreateNewContext(features);if(!ctx)return 2;
 SmokeSignalDelegator signals{0};auto sys=fextl::make_unique<SmokeSyscallHandler>();ctx->SetSignalDelegator(&signals);ctx->SetSyscallHandler(sys.get());ctx->EnableExitOnHLT();if(!ctx->InitCore())return 3;memory("context");
 const size_t page=sysconf(_SC_PAGESIZE);Mapping code(page,PROT_READ|PROT_WRITE),stack(page,PROT_READ|PROT_WRITE);if(!code.IsValid()||!stack.IsValid())return 4;
 uint64_t seed=0x1234567812345678ULL,count=50000000;unsigned char bytes[]={0x48,0xb8,0,0,0,0,0,0,0,0,0x48,0xb9,0,0,0,0,0,0,0,0,0x48,0x69,0xc0,0x0d,0x66,0x19,0x00,0x48,0x05,0x5f,0xf3,0x6e,0x3c,0x48,0xc1,0xc8,0x0d,0x48,0xff,0xc9,0x75,0xea,0xf4};std::memcpy(bytes+2,&seed,8);std::memcpy(bytes+12,&count,8);std::memcpy(code.Get(),bytes,sizeof(bytes));
 auto*t=ctx->CreateThread((uint64_t)code.Get(),(uint64_t)stack.Get()+page-16);if(!t)return 5;CallRetStack cr; if(!cr.IsReserved()||!cr.MakeWritable())return 6;cr.Initialize(t);GuestSegmentState seg;seg.Initialize(t->CurrentFrame->State);memory("one_thread");
 if(argc>1&&std::string_view(argv[1])=="darwin-registers"){RegisterInspector inspector(static_cast<FEXCore::Context::ContextImpl*>(ctx.get()));bool ok=inspector.valid();std::printf("DARWIN reserved_x18_excluded=%d\n",ok);ctx->DestroyThread(t);ctx.reset();return ok?0:12;}
 if(argc>1&&std::string_view(argv[1])=="jit-clear") {
  for(uint64_t round=0;round<16;++round) {
   auto* shadow=static_cast<unsigned char*>(t->CallRetStackBase);
   const size_t probes[]={0,FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE/4,FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE-8};
   for(auto offset:probes)std::memset(shadow+offset,0xa5,8);
   // ClearCodeCache is normally called inside CompileCode's Darwin JIT write scope.
   { FEXCore::Allocator::ScopedJITWriteProtect write;ctx->ClearCodeCache(t); }
   for(auto offset:probes){uint64_t value;std::memcpy(&value,shadow+offset,8);if(value){std::printf("CALLRET stale_after_clear=1 offset=%zu\n",offset);return 11;}}
   const uint64_t expected=0x12340000+round;
   unsigned char tiny[]={0x48,0xb8,0,0,0,0,0,0,0,0,0xf4};
   std::memcpy(tiny+2,&expected,8);std::memcpy(code.Get(),tiny,sizeof(tiny));
   t->CurrentFrame->State.rip=(uint64_t)code.Get();ctx->ExecuteThread(t);
   if(t->CurrentFrame->State.gregs[FEXCore::X86State::REG_RAX]!=expected)return 10;
  }
  std::puts("JIT clear_recompile_execute=16 changed_code_correct=1");
  memory("after_16_jit_clears");ctx->DestroyThread(t);ctx.reset();return 0;
 }
 if(lookup){auto&c=*t->LookupCache;bool ok=true;int attempted=0;
  for(int round=0;round<256;++round){++attempted;uint64_t addr=0x12345000+(round%16)*4096,host=0x76543000+round*16;c.AddBlockMapping(t,addr,{},(void*)host);
   auto before=c.FindBlock(t,addr);auto* l1=(FEXCore::LookupCache::LookupCacheEntry*)c.GetL1Pointer();auto index=addr&(c.GetScaledL1PointerMask()/sizeof(*l1));l1[index].GuestCode=0;
   ok&=c.FindBlock(t,addr)==host; // warms L2 through the real L3 lookup
   {auto lk=c.AcquireWriteLock();c.ClearL2Cache(lk);}ok&=((uintptr_t*)c.GetPagePointer())[(addr&(c.GetVirtualMemorySize()-1))>>12]==0;
   {auto lk=c.AcquireWriteLock();c.ClearCache(lk);}ok&=c.FindBlock(t,addr)==0;
   if(!ok)break;
  }
  std::printf("LOOKUP repeated_clear_refill=%d stale_free=%d\n",attempted,ok);memory("after_256_clears");ctx->DestroyThread(t);ctx.reset();memory("shutdown");return ok?0:7;}

 for(int i=0;i<5;i++){t->CurrentFrame->State.rip=(uint64_t)code.Get();auto start=std::chrono::steady_clock::now();ctx->ExecuteThread(t);auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();auto value=t->CurrentFrame->State.gregs[FEXCore::X86State::REG_RAX];std::printf("ARITH run=%d iterations=%llu ns=%lld result=%#llx correct=%d\n",i,(unsigned long long)count,(long long)ns,(unsigned long long)value,value==reference(seed,count));if(value!=reference(seed,count))return 8;}memory("warm");ctx->DestroyThread(t);memory("thread_destroyed");
 for(int i=0;i<32;i++){auto*q=ctx->CreateThread((uint64_t)code.Get(),(uint64_t)stack.Get()+page-16);if(!q)return 9;ctx->DestroyThread(q);}memory("after_32_thread_cycles");
 std::vector<FEXCore::Core::InternalThreadState*> threads;for(int i=0;i<4;i++){auto*q=ctx->CreateThread((uint64_t)code.Get(),(uint64_t)stack.Get()+page-16);if(!q)return 10;threads.push_back(q);}memory("four_threads");for(auto*q:threads)ctx->DestroyThread(q);memory("four_destroyed");ctx.reset();memory("shutdown");return 0;
}
