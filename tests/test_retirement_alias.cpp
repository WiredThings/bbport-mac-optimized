// SPDX-License-Identifier: GPL-2.0-or-later
// Diagnostic boundary test: virtual ownership is not a physical-alias proof.
// Uses real runtime mappings and Tracker; never creates or frees a texture.
#include "bbport_resource_lifetime.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <chrono>
extern "C" void alias95_create(uintptr_t*,uintptr_t*,uint64_t*,uint64_t*);
extern "C" void alias95_unmap(uintptr_t);
extern "C" int runtime_memory_reclaim_if_unaliased_v2(uintptr_t,uint64_t,int(*)(void*),void*);
int main(){uintptr_t a,b;uint64_t pa,pb;alias95_create(&a,&b,&pa,&pb);
 using namespace BbResourceLifetime;Tracker tracker;
 auto old=tracker.Allocate(Domain::Heap,a,16384);auto live_alias=tracker.Allocate(Domain::Heap,b,16384);
 assert(old && live_alias && tracker.Release(old));
 assert(tracker.Status(live_alias)==State::Live);
 auto inspection=tracker.InspectRange(old,a,16384);
 assert(inspection.valid && !inspection.overlaps[0] && !inspection.overlaps[1]);
 std::this_thread::sleep_for(std::chrono::milliseconds(2100));
 bool permitted=tracker.ReleasedRangeUnused(old,a,16384,2000),callback=false;
 bool committed=tracker.WithReleased(old,a,16384,2000,[&]{callback=true;return uint64_t(16384);});
 assert(permitted && committed && callback && pa==pb);
 bool guarded_callback=false;
 struct Context {Tracker* tracker;Tag old;uintptr_t address;bool* called;};
 Context context{&tracker,old,a,&guarded_callback};
 const int guarded=runtime_memory_reclaim_if_unaliased_v2(a,16384,[](void* opaque) noexcept {
  auto& c=*static_cast<Context*>(opaque);*c.called=true;
  return c.tracker->WithReleased(c.old,c.address,16384,2000,[]{return uint64_t(16384);}) ? 1 : 0;
 },&context);
 assert(!guarded && !guarded_callback);
 alias95_unmap(b);
 assert(tracker.Status(live_alias)==State::Live); // VM unmap is not a heap release.
 assert(tracker.ReleasedRangeUnused(old,a,16384,2000));
 assert(!runtime_memory_reclaim_if_unaliased_v2(a,16384,[](void* opaque) noexcept {
  *static_cast<bool*>(opaque)=true;return 1;
 },&guarded_callback));
 assert(!guarded_callback);

 printf("ALIAS95 {\"virtual_a\":%llu,\"virtual_b\":%llu,\"physical_a\":%llu,\"physical_b\":%llu,\"live_alias\":true,\"cpu_shared_bytes_verified\":true,\"virtual_overlap_count\":0,\"virtual_preflight_permits\":true,\"virtual_transaction_permits\":true,\"mapping_transaction_refused_live_alias\":true,\"mapping_history_refused_unmapped_live_owner\":true,\"actual_texture_freed\":false}\n",(unsigned long long)a,(unsigned long long)b,(unsigned long long)pa,(unsigned long long)pb);
}
