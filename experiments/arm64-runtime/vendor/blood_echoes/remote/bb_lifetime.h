// SPDX-License-Identifier: GPL-2.0-or-later
// Fixed-layout lifetime metadata shared by Rosetta and ARM64. No guest payload copies.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
namespace BbRemote {
struct LifetimeEvent {
    std::uint64_t address{}, size{}, generation{};
    std::uint32_t kind{}, domain{};
};
static_assert(sizeof(LifetimeEvent) == 32 && std::is_trivially_copyable_v<LifetimeEvent>);
inline constexpr std::size_t LifetimeCapacity = 8192;
struct LifetimeJournal {
    alignas(64) std::atomic<std::uint64_t> written{0};
    alignas(64) std::atomic<std::uint64_t> consumed{0};
    alignas(64) std::atomic<std::uint32_t> lost{0};
    LifetimeEvent events[LifetimeCapacity]{};
    // Producers serialize outside shared memory; the single consumer never waits
    // on the producer's allocation mutex. Overflow permanently vetoes reclamation.
    bool Push(LifetimeEvent value) {
        const auto head=written.load(std::memory_order_relaxed);
        const auto tail=consumed.load(std::memory_order_acquire);
        if(head-tail>=LifetimeCapacity) { lost.store(1,std::memory_order_release);return false; }
        events[head%LifetimeCapacity]=value;
        written.store(head+1,std::memory_order_release);return true;
    }
    bool Pop(LifetimeEvent& value) {
        const auto tail=consumed.load(std::memory_order_relaxed);
        if(tail==written.load(std::memory_order_acquire))return false;
        value=events[tail%LifetimeCapacity];
        consumed.store(tail+1,std::memory_order_release);return true;
    }
};
enum LeaseStage : std::uint32_t { LeaseIdle, LeaseRequest, LeaseClaimed, LeaseGranted,
                                LeaseComplete, LeaseFinished, LeaseRejected };
struct LifetimeLease {
    alignas(64) std::atomic<std::uint32_t> stage{LeaseIdle};
    LifetimeEvent owner{};
    std::uint64_t address{}, size{}, age_ms{}, bytes{};
    // Request can only be cancelled before the frontend claims it. Once granted,
    // its allocation lock must survive until Complete, including slow scheduling.
    bool CancelUnclaimed() {
        std::uint32_t expected=LeaseRequest;
        return stage.compare_exchange_strong(expected,LeaseIdle,std::memory_order_acq_rel);
    }
};
struct LifetimeShared {
    LifetimeJournal journal;
    LifetimeLease lease;
    alignas(64) std::atomic<std::uint32_t> hooks_requested{0};
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(sizeof(LifetimeShared) < (512u<<10));
} // namespace BbRemote
