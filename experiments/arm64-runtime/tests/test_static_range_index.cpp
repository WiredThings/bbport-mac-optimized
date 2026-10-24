// SPDX-License-Identifier: GPL-2.0-or-later
#include "static_range_index.h"
#include <cassert>
#include <random>
#include <thread>
#include <atomic>
#include <chrono>
#include <shared_mutex>
#include <cstdio>
struct Range{uintptr_t Begin;size_t Size;bool Executable,Writable;};
int main(){
 assert(!ScopedGuestStack::Contains(100,4));
 {ScopedGuestStack first(100,32);assert(ScopedGuestStack::Contains(100,32)&&!ScopedGuestStack::Contains(130,4));
  std::thread other([]{assert(!ScopedGuestStack::Contains(100,4));});other.join();
  {ScopedGuestStack nested(200,16);assert(ScopedGuestStack::Contains(200,16)&&!ScopedGuestStack::Contains(100,1));}
  assert(ScopedGuestStack::Contains(100,32));
 }
 assert(!ScopedGuestStack::Contains(100,4));
 std::vector<Range> ranges={{100,100,false,false},{120,10,false,true},{500,20,false,true},{520,20,false,true},{UINTPTR_MAX-32,16,false,true}};
 StaticRangeIndex fixed;fixed.Build(ranges);
 assert(fixed.Contains(150,30,false)&&!fixed.Contains(150,30,true));
 assert(fixed.Contains(120,10,true)&&!fixed.Contains(120,11,true));
 assert(!fixed.Contains(519,2,true)); // adjacent ranges do not silently widen authorization
 assert(!fixed.Contains(0,1,false)&&!fixed.Contains(120,0,false));
 assert(!fixed.Contains(UINTPTR_MAX-8,32,false));
 bool twice=false;try{fixed.Build(ranges);}catch(...){twice=true;}assert(twice);
 std::mt19937_64 rng(5133);
 for(unsigned i=0;i<350;++i)ranges.push_back({1+rng()%65536,1+rng()%4096,false,bool(rng()&1)});
 StaticRangeIndex index;index.Build(ranges);
 auto old=[&](uintptr_t a,size_t n,bool w){if(!a||!n||a>UINTPTR_MAX-n)return false;for(auto r:ranges)if(a>=r.Begin&&n<=r.Size&&a-r.Begin<=r.Size-n&&(!w||r.Writable))return true;return false;};
 for(unsigned i=0;i<200000;++i){auto a=rng()%70000;auto n=rng()%5000;bool w=rng()&1;assert(index.Contains(a,n,w)==old(a,n,w));}
 std::shared_mutex lock;std::atomic<unsigned long> hits{};
 for(bool fast:{false,true}){
  auto start=std::chrono::steady_clock::now();std::vector<std::thread> threads;
  for(unsigned t=0;t<8;++t)threads.emplace_back([&,fast]{unsigned count=0;for(unsigned i=0;i<100000;++i){auto a=1+(i*37)%65536;if(fast)count+=index.Contains(a,8,i&1);else{std::shared_lock hold(lock);count+=old(a,8,i&1);}}hits+=count;});
  for(auto& t:threads)t.join();printf("STATIC RANGE equivalent checks, eight threads: %s %.3f ms, metadata=%zu bytes\n",fast?"immutable-index":"locked-linear",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),index.Bytes());
 }
 assert(hits>0);
}
