#include "core/guest_cpu/hle_call_adapter.h"
#include <cassert>
#include <thread>
#include <chrono>
#include <cstdio>
using namespace Core::GuestCpu;
static std::atomic<unsigned> deleted{};
struct Adapter: HleCallAdapter {~Adapter(){++deleted;}HleCallResult Invoke(HleCallFrame&) const override{return true;}};
static double bench(bool fast){setenv("BB_ARM_HLE_FAST",fast?"1":"0",1);HleCallRegistry registry;for(int i=0;i<64;i++)registry.Register(std::make_shared<Adapter>(),"bench");
 std::atomic<bool>go{};std::vector<std::thread>threads;auto start=std::chrono::steady_clock::now();for(int t=0;t<8;t++)threads.emplace_back([&,t]{while(!go.load(std::memory_order_acquire))std::this_thread::yield();for(unsigned i=0;i<20000;i++){auto*p=registry.FindBorrowed((i+t)%64+1);assert(p&&p->Name()=="bench");}});start=std::chrono::steady_clock::now();go.store(true,std::memory_order_release);for(auto&t:threads)t.join();return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
int main(){
 setenv("BB_ARM_HLE_FAST","1",1);std::weak_ptr<HleCallAdapter> lifetime;unsigned before=deleted.load();
 {
  HleCallRegistry registry;assert(!registry.FindBorrowed(0)&&!registry.FindBorrowed(1)&&!registry.FindBorrowed(UINT64_MAX));
  auto first=registry.Register(std::make_shared<Adapter>(),"first");lifetime=first;assert(first&&first->Operation()==1&&registry.FindBorrowed(1)==first.get());first.reset();
  std::atomic<unsigned>writers{};std::atomic<bool>start{};std::vector<std::thread>readers,producers;
  for(unsigned t=0;t<8;t++)readers.emplace_back([&,t]{while(!start.load())std::this_thread::yield();unsigned i=t;while(writers.load()<4){u64 op=(++i*2654435761ULL)%10001+1;auto*p=registry.FindBorrowed(op);if(p){assert(p->Operation()==op);assert(!p->Name().empty());}}});
  for(unsigned t=0;t<4;t++)producers.emplace_back([&,t]{while(!start.load())std::this_thread::yield();for(unsigned i=0;i<2500;i++){auto a=registry.Register(std::make_shared<Adapter>(),"registered-"+std::to_string(t));assert(a&&a->Operation());}++writers;});start=true;
  for(auto&t:producers)t.join();for(auto&t:readers)t.join();
  for(u64 op=1;op<=10001;op++){auto owned=registry.Find(op);assert(owned&&owned->Operation()==op&&registry.FindBorrowed(op)==owned.get());}
  assert(!registry.FindBorrowed(10002)&&!registry.FindBorrowed(UINT64_MAX));assert(!lifetime.expired());
 }
 assert(lifetime.expired()&&deleted.load()-before==10001);
 // Same-address reconstruction cannot return an earlier registry's adapter.
 alignas(HleCallRegistry)std::byte storage[sizeof(HleCallRegistry)];auto*r=new(storage)HleCallRegistry;auto a=r->Register(std::make_shared<Adapter>(),"old");assert(r->FindBorrowed(1)==a.get());r->~HleCallRegistry();r=new(storage)HleCallRegistry;assert(!r->FindBorrowed(1));auto b=r->Register(std::make_shared<Adapter>(),"new");assert(r->FindBorrowed(1)==b.get());r->~HleCallRegistry();
 const double old=bench(false),fast=bench(true);printf("published HLE lookup passed: 10001 registrations incl overflow, concurrent readers, ownership release, same-address reuse; 160000 lookups legacy=%.3fms fast=%.3fms; root=%zu bytes\n",old,fast,sizeof(HleCallRegistry));
}
