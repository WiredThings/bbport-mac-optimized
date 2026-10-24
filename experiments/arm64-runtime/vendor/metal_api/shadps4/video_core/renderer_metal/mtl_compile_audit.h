// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in timing only; no source, resource payload or persistent history.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace Metal {
struct CompileAudit {
    static bool Enabled() {
        static const bool enabled = [] {
            const char* value = std::getenv("BB_MTL_COMPILE_AUDIT");
            return value && value[0] == '1';
        }();
        return enabled;
    }
    static uint64_t Now() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    const char* kind;
    uint64_t id, start;
    bool ok{};
    int cached{-1};
    CompileAudit(const char* kind_, uint64_t id_ = 0)
        : kind{kind_}, id{id_}, start{Enabled() ? Now() : 0} {}
    ~CompileAudit() {
        if (!Enabled()) return;
        const auto end = Now();
        static std::atomic<unsigned> lines{};
        const auto line = lines.fetch_add(1, std::memory_order_relaxed);
        if (line < 8192)
            std::printf("Metal compile audit: kind=%s id=%016llx start-ns=%llu elapsed-us=%llu ok=%d cached=%d\n",
                kind, (unsigned long long)id, (unsigned long long)start,
                (unsigned long long)((end-start)/1000), int(ok), int(cached));
        else if (line == 8192)
            std::puts("Metal compile audit: truncated=1");
    }
};
} // namespace Metal
