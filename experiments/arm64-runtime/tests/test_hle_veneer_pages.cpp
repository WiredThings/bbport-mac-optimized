// Native VM / actual allocator regression. Never executes x86 bytes on ARM.
#include "core/guest_cpu/hle_call_adapter.h"
#include "bb_cpu_cache_inputs.h"
#include <mach/mach_vm.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <csignal>
#include <cstdio>
#include <set>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
using namespace Core::GuestCpu;
struct Adapter: HleCallAdapter {HleCallResult Invoke(HleCallFrame&)const override{return true;}};
static vm_region_basic_info_data_64_t info(uintptr_t at,bool* mapped=nullptr){
 mach_vm_address_t address=at;mach_vm_size_t size=0;vm_region_basic_info_data_64_t data{};
 mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=MACH_PORT_NULL;
 auto k=mach_vm_region(mach_task_self(),&address,&size,VM_REGION_BASIC_INFO_64,reinterpret_cast<vm_region_info_t>(&data),&count,&object);
 if(object)assert(mach_port_deallocate(mach_task_self(),object)==KERN_SUCCESS);
 bool present=k==KERN_SUCCESS&&address<=at&&at-address<size;if(mapped)*mapped=present;else assert(present);return data;
}
static bool present(uintptr_t a){bool result=false;info(a,&result);return result;}
static uintptr_t failed_writer,failed_reader;static unsigned protect_calls;
static kern_return_t fail_remap(uintptr_t a,size_t,uintptr_t*){failed_writer=a;return KERN_NO_SPACE;}
static kern_return_t save_remap(uintptr_t a,size_t n,uintptr_t* b){failed_writer=a;auto k=BbDarwinVeneer::Slab::NativeRemap(a,n,b);if(!k)failed_reader=*b;return k;}
static kern_return_t fail_protect(uintptr_t,size_t,bool,vm_prot_t){return KERN_PROTECTION_FAILURE;}
static kern_return_t fail_second_protect(uintptr_t a,size_t n,bool maximum,vm_prot_t prot){return ++protect_calls==2?KERN_PROTECTION_FAILURE:BbDarwinVeneer::Slab::NativeProtect(a,n,maximum,prot);}
static void factory_tests(){
 using S=BbDarwinVeneer::Slab;
 assert(std::get<int>(S::Create(0))==EINVAL&&std::get<int>(S::Create(32768))==EINVAL);
 assert(std::get<int>(S::Create(16384,nullptr))==EINVAL);
 assert(std::get<int>(S::Create(16384,fail_remap))==ENOMEM&&!present(failed_writer));
 assert(std::get<int>(S::Create(16384,save_remap,fail_protect))==EACCES&&!present(failed_writer)&&!present(failed_reader));
 protect_calls=0;assert(std::get<int>(S::Create(16384,save_remap,fail_second_protect))==EACCES&&protect_calls==2&&!present(failed_writer)&&!present(failed_reader));
 uintptr_t ro,rw;
 {
  auto made=S::Create(16384);assert(std::holds_alternative<std::unique_ptr<S>>(made));auto slab=std::move(std::get<std::unique_ptr<S>>(made));ro=(uintptr_t)slab->Reader();rw=(uintptr_t)slab->Writer();assert(ro!=rw);
  assert(info(ro).protection==VM_PROT_READ&&info(ro).max_protection==VM_PROT_READ);
  assert(info(rw).protection==(VM_PROT_READ|VM_PROT_WRITE));
  for(unsigned i=0;i<16384;i++)slab->Writer()[i]=static_cast<unsigned char>(i*13);
  for(unsigned i=0;i<16384;i++)assert(slab->Reader()[i]==static_cast<unsigned char>(i*13));
  assert(mach_vm_protect(mach_task_self(),ro,16384,false,VM_PROT_READ|VM_PROT_WRITE)==KERN_PROTECTION_FAILURE);
  assert(mach_vm_protect(mach_task_self(),ro,16384,false,VM_PROT_READ|VM_PROT_EXECUTE)==KERN_PROTECTION_FAILURE);
  assert(info(ro).protection==VM_PROT_READ);slab->Writer()[16000]=57;assert(slab->Reader()[16000]==57);
 }
 assert(!present(ro)&&!present(rw));
}
static void code_ok(uintptr_t address,uint64_t operation){
 unsigned char bytes[16];std::memcpy(bytes,(void*)address,16);assert(bytes[0]==0x49&&bytes[1]==0x89&&bytes[2]==0xca&&bytes[3]==0x48&&bytes[4]==0xb8&&bytes[13]==0x0f&&bytes[14]==0x05&&bytes[15]==0xc3);
 uint64_t got;std::memcpy(&got,bytes+5,8);assert(got==operation);
}
int main(int argc,char**argv){
 const bool packed=argc==2&&std::string_view(argv[1])=="packed";assert(packed||(argc==2&&std::string_view(argv[1])=="legacy"));setenv("BB_ARM_PACKED_VENEERS",packed?"1":"0",1);factory_tests();
 constexpr unsigned N=2050;std::array<std::atomic<uintptr_t>,N> slots{};std::vector<std::shared_ptr<HleCallAdapter>> adapters;std::vector<Core::GuestExecutionRange> ranges;std::set<uintptr_t> l1_slots;
 HleCallRegistry registry;for(unsigned i=0;i<N;i++)adapters.push_back(registry.Register(std::make_shared<Adapter>(),"test.veneer"));
 {
  HleVeneerAllocator allocator;Adapter unregistered;auto bad=allocator.Allocate(unregistered);assert(std::holds_alternative<HleVeneerFailure>(bad)&&std::get<HleVeneerFailure>(bad).error==EINVAL&&allocator.GetExecutableRanges().empty());
  assert(std::string_view(allocator.LayoutName())==(packed?"darwin-packed-guest-ro-v1":"standalone-native-rx-v1"));
  std::atomic<unsigned> writers=0;std::atomic<bool> start=false;std::vector<std::thread> threads;
  for(unsigned t=0;t<4;t++)threads.emplace_back([&,t]{while(!start.load(std::memory_order_acquire))std::this_thread::yield();unsigned i=t;while(writers.load(std::memory_order_acquire)<8){i=(i+17)%N;auto p=slots[i].load(std::memory_order_acquire);if(p){code_ok(p,i+1);auto range=allocator.QueryExecutableRange(p);assert(range&&range->Executable&&!range->Writable&&p>=range->Begin&&p-range->Begin<range->Size);}}});
  for(unsigned t=0;t<8;t++)threads.emplace_back([&,t]{while(!start.load(std::memory_order_acquire))std::this_thread::yield();for(unsigned i=t;i<N;i+=8){auto result=allocator.Allocate(*adapters[i]);assert(std::holds_alternative<uint64_t>(result));auto address=std::get<uint64_t>(result);code_ok(address,i+1);slots[i].store(address,std::memory_order_release);assert(std::get<uint64_t>(allocator.Allocate(*adapters[i]))==address);}writers.fetch_add(1,std::memory_order_release);});
  start.store(true,std::memory_order_release);for(auto&t:threads)t.join();ranges=allocator.GetExecutableRanges();assert(ranges.size()==(packed?3:N));std::set<uintptr_t> addresses;
  for(unsigned i=0;i<N;i++){auto address=slots[i].load();code_ok(address,i+1);assert(addresses.insert(address).second);l1_slots.insert(address&8191);}
  for(auto range:ranges){assert(range.Size==16384&&range.Executable&&!range.Writable);auto vm=info(range.Begin);assert(vm.protection==(packed?VM_PROT_READ:VM_PROT_READ|VM_PROT_EXECUTE));if(packed)assert(vm.max_protection==VM_PROT_READ);}
  // A real native write through the guest alias must fault. Child catches it;
  // no coredump, no game process, and no ARM execution of the x86 stub.
  pid_t child=fork();assert(child>=0);
  if(!child){struct rlimit limit{0,0};setrlimit(RLIMIT_CORE,&limit);struct sigaction action{};action.sa_handler=+[](int){_exit(86);};sigemptyset(&action.sa_mask);sigaction(SIGBUS,&action,nullptr);sigaction(SIGSEGV,&action,nullptr);*reinterpret_cast<volatile unsigned char*>(slots[0].load())=0;_exit(87);}
  int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==86);
  auto digest=BbCpuCache::Hash(std::as_bytes(std::span("authored-build",14)));auto a=BbCacheInputs::RegistryABI(digest,registry,sizeof(HleCallFrame),alignof(HleCallFrame),"standalone-native-rx-v1");auto b=BbCacheInputs::RegistryABI(digest,registry,sizeof(HleCallFrame),alignof(HleCallFrame),"darwin-packed-guest-ro-v1");assert(a!=b);
 }
 for(auto r:ranges)assert(!present(r.Begin));assert(l1_slots.size()==(packed?512:1));
 printf("VENEER_PAGES layout=%s stubs=%u guest_pages=%zu guest_virtual_bytes=%zu min_l1_indices=%zu concurrent_immutable=1 factory_readonly_max=1 partial_failure_cleanup=1 released=1 identity_separated=1 passed=1\n",packed?"packed":"legacy",N,ranges.size(),ranges.size()*16384,l1_slots.size());
}
