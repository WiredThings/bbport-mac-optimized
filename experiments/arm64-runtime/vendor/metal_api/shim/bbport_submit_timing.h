// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unistd.h>

namespace BbSubmitTiming {
// Private, bounded metadata only. Never enabled by a normal launch. All timestamps use
// CACurrentMediaTime / MTLCommandBuffer GPU time (seconds), not translated x86 TSC values.
constexpr unsigned Capacity = 2048;
struct Entry {
    std::uint64_t queue, tick;
    unsigned frame, role;
    double commit, start, end, callback;
};
struct State {
    const char* path = std::getenv("BB_MTL_SUBMIT_AUDIT");
    const char* trigger = std::getenv("BB_MTL_SUBMIT_TRIGGER");
    std::unique_ptr<std::array<Entry, Capacity>> entries = path && trigger
        ? std::make_unique<std::array<Entry, Capacity>>() : nullptr;
    std::atomic<bool> active{false};
    std::atomic<unsigned> issued{0}, completed{0};
    // Only the first caller for a frame polls the trigger, across both queue roles.
    std::atomic<unsigned> checked_frame{~0u};
};
inline State& Get() { static State s; return s; }
inline unsigned Begin(unsigned frame) {
    auto& s = Get();
    if (!s.entries) return Capacity;
    if (!s.active.load(std::memory_order_acquire)) {
        if (s.checked_frame.exchange(frame, std::memory_order_relaxed) == frame ||
            access(s.trigger, F_OK) != 0) return Capacity;
        s.active.store(true, std::memory_order_release);
    }
    // Saturate; diagnostics cannot overflow a counter during a long session.
    unsigned i=s.issued.load(std::memory_order_relaxed);
    while(i<Capacity && !s.issued.compare_exchange_weak(i,i+1,std::memory_order_relaxed)) {}
    return i;
}
inline void Complete(unsigned index, const Entry& entry) {
    if (index>=Capacity) return;
    auto& s=Get();
    (*s.entries)[index]=entry;
    // The release sequence publishes every completed slot before the final writer reads it.
    if(s.completed.fetch_add(1,std::memory_order_acq_rel)+1 != Capacity) return;
    const std::string pending=std::string{s.path}+".tmp";
    if(FILE* f=std::fopen(pending.c_str(),"w")) {
        std::fprintf(f,"queue,tick,frame,role,commit_s,start_s,end_s,callback_s\n");
        for(const auto& e:*s.entries)
            std::fprintf(f,"%llu,%llu,%u,%u,%.9f,%.9f,%.9f,%.9f\n",
                (unsigned long long)e.queue,(unsigned long long)e.tick,e.frame,e.role,e.commit,e.start,e.end,e.callback);
        if(std::fclose(f)==0)std::rename(pending.c_str(),s.path);
    }
}
} // namespace BbSubmitTiming
