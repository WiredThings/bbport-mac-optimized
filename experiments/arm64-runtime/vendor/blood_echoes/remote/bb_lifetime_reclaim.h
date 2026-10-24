// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "remote/bb_lifetime.h"
#include "bbport_resource_lifetime.h"
namespace BbRemote {
// Dedicated frontend worker, independent of protection/command readers. This
// lock order is essential: removing cache entries can itself request protection.
inline bool ServeLifetimeLease(LifetimeShared& shared,BbResourceLifetime::Tracker& tracker,bool ready) {
    auto& lease=shared.lease;
    std::uint32_t expected=LeaseRequest;
    if(!lease.stage.compare_exchange_strong(expected,LeaseClaimed,std::memory_order_acq_rel))return false;
    const auto owner=lease.owner;
    const BbResourceLifetime::Tag tag{owner.address,owner.size,owner.generation,
                                    BbResourceLifetime::Domain(owner.domain)};
    bool entered=false;
    if(!shared.journal.lost.load(std::memory_order_acquire) && ready) {
        tracker.WithReleasedLocal(tag,lease.address,lease.size,lease.age_ms,[&] {
            entered=true;
            lease.stage.store(LeaseGranted,std::memory_order_release);
            // No timeout after grant: the lock must remain held until the
            // backend is finished. A dead peer is handled by process supervision.
            while(lease.stage.load(std::memory_order_acquire)!=LeaseComplete)
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            return lease.bytes;
        });
    }
    lease.stage.store(entered?LeaseFinished:LeaseRejected,std::memory_order_release);
    return true;
}
// Caller serializes requests. Both the authoritative frontend and mirrored
// backend must validate; overflow, reuse, missing or overlapping records veto.
inline bool ReclaimWithLifetimeLease(LifetimeShared& shared,BbResourceLifetime::Tracker& tracker,
                                    BbResourceLifetime::Tag tag,std::uint64_t address,
                                    std::uint64_t size,std::uint64_t age,
                                    const std::function<std::uint64_t()>& reclaim) {
    if(shared.journal.lost.load(std::memory_order_acquire))return false;
    auto& lease=shared.lease;
    if(lease.stage.load(std::memory_order_acquire)!=LeaseIdle)return false;
    lease.owner={tag.address,tag.size,tag.generation,0,std::uint32_t(tag.domain)};
    lease.address=address;lease.size=size;lease.age_ms=age;lease.bytes=0;
    lease.stage.store(LeaseRequest,std::memory_order_release);
    const auto started=std::chrono::steady_clock::now();
    std::uint32_t stage;
    while((stage=lease.stage.load(std::memory_order_acquire))!=LeaseGranted && stage!=LeaseRejected) {
        if(std::chrono::steady_clock::now()-started>std::chrono::milliseconds(250) && lease.CancelUnclaimed())return false;
        if(std::chrono::steady_clock::now()-started>std::chrono::seconds(10)) {
            std::fprintf(stderr,"GPU lifetime: claimed lease stalled; stopping owned backend\n");
            std::_Exit(70);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    if(stage==LeaseRejected) {lease.stage.store(LeaseIdle,std::memory_order_release);return false;}
    bool removed=false;
    try {
        removed=tracker.WithReleasedLocal(tag,address,size,age,[&] {
            const auto bytes=reclaim();lease.bytes=bytes;return bytes;
        });
    } catch(...) { std::_Exit(70); }
    lease.stage.store(LeaseComplete,std::memory_order_release);
    while(lease.stage.load(std::memory_order_acquire)!=LeaseFinished)
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    lease.stage.store(LeaseIdle,std::memory_order_release);
    return removed;
}
} // namespace BbRemote
