// Actual FEX -> generated x86 veneer -> typed HLE, including a post-start slab.
#include "core/guest_cpu/fex_guest_cpu.h"
#include "core/guest_cpu/fex_hle_bridge.h"
#include <FEXCore/Core/X86Enums.h>
#include <cassert>
#include <bit>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
using namespace Core;using namespace Core::GuestCpu;
struct Pair{u64 a,b;};struct Doubles{double a,b;};
namespace Core::GuestCpu {
template<>struct GuestAggregate<Pair>{static constexpr bool enabled=true;static constexpr auto fields=std::tuple{GuestField<u64,0>{},GuestField<u64,8>{}};};
template<>struct GuestAggregate<Doubles>{static constexpr bool enabled=true;static constexpr auto fields=std::tuple{GuestField<double,0>{},GuestField<double,8>{}};};
}
struct Mapping{void* ptr;size_t size;explicit Mapping(size_t n):size(n){ptr=mmap(nullptr,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(ptr!=MAP_FAILED);}~Mapping(){assert(!munmap(ptr,size));}uintptr_t at()const{return reinterpret_cast<uintptr_t>(ptr);}};
struct State{HleCallRegistry registry;HleVeneerAllocator veneers;std::vector<GuestExecutionRange> ranges,initial;std::unique_ptr<FexGuestCpuBackend> cpu;uintptr_t callback{},late{};unsigned factories{},nested{},tails{},seven{},pair{},doubles{},pointer{};};
static State* state;
static bool valid(void*ctx,uintptr_t address,size_t size,bool write){auto&s=*static_cast<State*>(ctx);if(!address||!size)return false;for(auto r:s.ranges)if((!write||r.Writable)&&address>=r.Begin&&address-r.Begin<r.Size&&size<=r.Size-(address-r.Begin))return true;return false;}
static std::optional<GuestExecutionRange> executable(void*ctx,uintptr_t address){auto&s=*static_cast<State*>(ctx);if(auto r=s.veneers.QueryExecutableRange(address))return r;for(auto r:s.ranges)if(r.Executable&&address>=r.Begin&&address-r.Begin<r.Size)return r;return std::nullopt;}
static u64 add(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g){++state->seven;return a+b+c+d+e+f+g;}
static u64 pair(Pair p){++state->pair;return p.a+p.b;}
static double doubles(Doubles p){++state->doubles;return p.a+p.b;}
static u64 pointer(const u64*p){++state->pointer;return *p+1;}
static u64 tail(){++state->tails;return 99;}
static uintptr_t allocate(std::shared_ptr<HleCallAdapter> a){auto value=state->veneers.Allocate(*a);assert(std::holds_alternative<u64>(value));return std::get<u64>(value);}
static u64 factory(){
 ++state->factories;auto target=state->registry.Register(MakeHleCallAdapter(tail),"fixture.dynamic.tail");state->late=allocate(target);
 assert(std::none_of(state->initial.begin(),state->initial.end(),[](auto r){return state->late>=r.Begin&&state->late-r.Begin<r.Size;}));
 std::array<u64,2> args{17,19};auto result=state->cpu->CallGuest(state->callback,args);assert(std::holds_alternative<GuestExecutionState>(result)&&std::get<GuestExecutionState>(result).Gpr[FEXCore::X86State::REG_RAX]==36);++state->nested;
 return state->late;
}
int main(int argc,char**argv){
 assert(argc==2);bool packed=std::string_view(argv[1])=="packed";assert(packed||std::string_view(argv[1])=="legacy");setenv("BB_ARM_PACKED_VENEERS",packed?"1":"0",1);
 State s;state=&s;Mapping code(16384),stack(65536);s.callback=code.at()+0x300;
 auto seven_stub=allocate(s.registry.Register(MakeHleCallAdapter(add),"fixture.seven"));auto pair_stub=allocate(s.registry.Register(MakeHleCallAdapter(pair),"fixture.pair"));auto doubles_stub=allocate(s.registry.Register(MakeHleCallAdapter(doubles),"fixture.doubles"));auto pointer_stub=allocate(s.registry.Register(MakeHleCallAdapter(pointer),"fixture.pointer"));auto factory_stub=allocate(s.registry.Register(MakeHleCallAdapter(factory),"fixture.factory"));
 // The host callback must allocate a NEW slab/page after the FEX thread started,
 // not merely append a slot covered by its bootstrap MappedRanges snapshot.
 for(unsigned i=5;i<1024;i++)allocate(s.registry.Register(MakeHleCallAdapter(+[]()->u64{return 0;}),"fixture.unused"));
 auto initial_pages=s.veneers.GetExecutableRanges();assert(initial_pages.size()==(packed?1:1024));
 std::array<unsigned char,80> original{};for(unsigned i=0;i<5;i++)std::memcpy(original.data()+i*16,reinterpret_cast<void*>(std::array<uintptr_t,5>{seven_stub,pair_stub,doubles_stub,pointer_stub,factory_stub}[i]),16);
 std::vector<unsigned char> bytes;std::vector<size_t> branches;
 auto emit=[&](std::initializer_list<unsigned char> v){bytes.insert(bytes.end(),v);};auto imm=[&](u64 value){auto n=bytes.size();bytes.resize(n+8);std::memcpy(bytes.data()+n,&value,8);};auto call=[&](uintptr_t p){emit({0x48,0xb8});imm(p);emit({0xff,0xd0});};auto check=[&](){emit({0x0f,0x85});branches.push_back(bytes.size());bytes.resize(bytes.size()+4);};
 emit({0x48,0x83,0xec,0x18,0x48,0xc7,0x04,0x24,7,0,0,0});
 emit({0xbf,1,0,0,0,0xbe,2,0,0,0,0xba,3,0,0,0,0xb9,4,0,0,0,0x41,0xb8,5,0,0,0,0x41,0xb9,6,0,0,0});call(seven_stub);emit({0x48,0x83,0xf8,28});check();
 emit({0xbf,11,0,0,0,0xbe,13,0,0,0});call(pair_stub);emit({0x48,0x83,0xf8,24});check();
 emit({0x48,0xb8});imm(std::bit_cast<u64>(1.25));emit({0x66,0x48,0x0f,0x6e,0xc0,0x48,0xb8});imm(std::bit_cast<u64>(2.75));emit({0x66,0x48,0x0f,0x6e,0xc8});call(doubles_stub);emit({0x66,0x48,0x0f,0x7e,0xc0,0x48,0xba});imm(std::bit_cast<u64>(4.0));emit({0x48,0x39,0xd0});check();
 emit({0x48,0xc7,0x04,0x24,41,0,0,0,0x48,0x89,0xe7});call(pointer_stub);emit({0x48,0x83,0xf8,42});check();
 call(factory_stub);emit({0xff,0xd0,0x48,0x83,0xf8,99});check();emit({0xb8,42,0,0,0,0x48,0x83,0xc4,0x18,0xc3});auto failure=bytes.size();emit({0x31,0xc0,0x48,0x83,0xc4,0x18,0xc3});for(auto at:branches){int32_t delta=failure-at-4;std::memcpy(bytes.data()+at,&delta,4);}assert(bytes.size()<0x300);
 std::memset(code.ptr,0xf4,code.size);std::memcpy(code.ptr,bytes.data(),bytes.size());const unsigned char callback[]={0x48,0x8d,0x04,0x37,0xc3};std::memcpy(static_cast<char*>(code.ptr)+0x300,callback,sizeof(callback));assert(!mprotect(code.ptr,code.size,PROT_READ));
 s.ranges={{code.at(),code.size,true,false},{stack.at(),stack.size,false,true}};
 HleGuestBridge bridge(s.registry,valid,&s,nullptr,nullptr,executable,&s);auto made=FexGuestCpuBackend::Create(bridge);assert(std::holds_alternative<std::unique_ptr<FexGuestCpuBackend>>(made));s.cpu=std::move(std::get<std::unique_ptr<FexGuestCpuBackend>>(made));
 s.ranges.push_back(s.cpu->ReturnRange());s.ranges.push_back(s.cpu->CallbackReturnRange());s.initial=s.ranges;s.initial.insert(s.initial.end(),initial_pages.begin(),initial_pages.end());
 GuestExecutionRequest req;req.Rip=code.at();req.Rsp=stack.at()+stack.size-8;req.Rflags=2;req.MappedRanges=s.initial;auto halt=s.cpu->ReturnAddress();std::memcpy((void*)req.Rsp,&halt,8);
 for(unsigned iteration=0;iteration<3;iteration++){
  auto r=s.cpu->Run(req);if(auto*f=std::get_if<GuestExecutionFailure>(&r)){printf("VENEER_EXEC failure stage=%u error=%d\n",unsigned(f->Stage),f->Error);return 1;}assert(std::get<GuestExecutionState>(r).Gpr[FEXCore::X86State::REG_RAX]==42);
  for(unsigned i=0;i<5;i++)assert(!std::memcmp(original.data()+i*16,reinterpret_cast<void*>(std::array<uintptr_t,5>{seven_stub,pair_stub,doubles_stub,pointer_stub,factory_stub}[i]),16));
  // Execute a NEW slot in the SAME page as a previously JITed tail (packed
  // layout). A page-level decoded snapshot must not hide later publication.
  auto sibling=allocate(s.registry.Register(MakeHleCallAdapter(tail),"fixture.new-same-page.tail"));
  if(packed)assert((sibling&~uintptr_t(16383))==(s.late&~uintptr_t(16383)));
  auto fresh=req;fresh.Rip=sibling;fresh.MappedRanges=s.ranges;auto freshRanges=s.veneers.GetExecutableRanges();fresh.MappedRanges.insert(fresh.MappedRanges.end(),freshRanges.begin(),freshRanges.end());
  auto siblingResult=s.cpu->Run(fresh);assert(std::holds_alternative<GuestExecutionState>(siblingResult)&&std::get<GuestExecutionState>(siblingResult).Gpr[FEXCore::X86State::REG_RAX]==99);
  for(unsigned i=0;i<1024;i++)allocate(s.registry.Register(MakeHleCallAdapter(+[]()->u64{return 0;}),"fixture.appended"));
  auto before=s.cpu->InspectStartupCodeCache();assert(std::holds_alternative<AetherPS4::Fex::StartupCpuCacheInfo>(before));
  // CallGuest is intentionally callback-only, not a general idle entry point.
  auto idle=s.cpu->CallGuest(s.late,{});assert(std::holds_alternative<GuestExecutionFailure>(idle)&&std::get<GuestExecutionFailure>(idle).Error==ENXIO);
  auto reuse=req;reuse.Rip=s.late;reuse.MappedRanges=s.ranges;auto current=s.veneers.GetExecutableRanges();reuse.MappedRanges.insert(reuse.MappedRanges.end(),current.begin(),current.end());
  auto reused=s.cpu->Run(reuse);assert(std::holds_alternative<GuestExecutionState>(reused)&&std::get<GuestExecutionState>(reused).Gpr[FEXCore::X86State::REG_RAX]==99);
  auto after=s.cpu->InspectStartupCodeCache();assert(std::holds_alternative<AetherPS4::Fex::StartupCpuCacheInfo>(after));assert(std::get<AetherPS4::Fex::StartupCpuCacheInfo>(before).jit_compiled_bytes==std::get<AetherPS4::Fex::StartupCpuCacheInfo>(after).jit_compiled_bytes);
  // Initialize a real thread before invalidation; do not touch uninitialized
  // call-ret/safepoint state. The next iteration must recompile the invalid page.
  auto leaf=req;leaf.Rip=s.callback;leaf.Gpr[FEXCore::X86State::REG_RDI]=17;leaf.Gpr[FEXCore::X86State::REG_RSI]=19;
  auto madeThread=s.cpu->CreateThread(leaf);assert(std::holds_alternative<std::unique_ptr<FexGuestCpuBackend::Thread>>(madeThread));auto thread=std::move(std::get<std::unique_ptr<FexGuestCpuBackend::Thread>>(madeThread));
  auto leafResult=s.cpu->Run(*thread);assert(std::holds_alternative<GuestExecutionState>(leafResult)&&std::get<GuestExecutionState>(leafResult).Gpr[FEXCore::X86State::REG_RAX]==36);
  auto badInvalidate=s.cpu->Invalidate(*thread,seven_stub,16);assert(std::holds_alternative<GuestExecutionFailure>(badInvalidate)&&std::get<GuestExecutionFailure>(badInvalidate).Error==EINVAL);
  auto sourceRange=s.veneers.QueryExecutableRange(seven_stub);assert(sourceRange);auto invalidated=s.cpu->Invalidate(*thread,sourceRange->Begin,sourceRange->Size);assert(std::holds_alternative<bool>(invalidated)&&std::get<bool>(invalidated));
  auto retired=s.cpu->DestroyThread(thread);assert(std::holds_alternative<bool>(retired)&&std::get<bool>(retired));
 }
 assert(s.factories==3&&s.nested==3&&s.tails==9&&s.seven==3&&s.pair==3&&s.doubles==3&&s.pointer==3);auto pages=s.veneers.GetExecutableRanges().size();s.cpu.reset();state=nullptr;
 printf("VENEER_EXEC layout=%s passes=3 seven_stack=3 integer_struct=3 sse_struct=3 checked_pointer=3 nested_callbacks=3 late_slabs=3 new_same_page_targets_executed=3 cached_late_target_reused=3 invalidated_guest_pages=3 invalid_bounds_rejected=3 idle_callbacks_rejected=3 no_old_tail_fallback_jit=3 old_slots_unchanged=1 guest_pages=%zu passed=1\n",packed?"packed":"legacy",pages);
}
