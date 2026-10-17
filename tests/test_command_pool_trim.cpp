// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_pool_trim.h"
#include <cassert>
#include <cstdio>
#include <numeric>

int main() {
    // A loading peak leaves 512 handles; 64 are still in flight. Only completed
    // handles beyond the 32-entry working set may be destroyed.
    std::vector<std::uint64_t> ticks(512, 10), handles(512);
    std::iota(handles.begin(), handles.end(), 1);
    for (std::size_t i = 0; i < ticks.size(); i += 8) ticks[i] = 11;
    const auto freed = BbTrimCompletedBuffers(ticks, handles, 10, 32);
    assert(freed.size() == 416 && ticks.size() == 96 && handles.size() == 96);
    for (const auto handle : freed) assert((handle - 1) % 8 != 0);
    std::size_t pending = 0;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        const bool in_flight = (handles[i] - 1) % 8 == 0;
        assert(ticks[i] == (in_flight ? 11 : 10));
        pending += in_flight;
    }
    assert(pending == 64);
    assert(BbTrimCompletedBuffers(ticks, handles, 10, 32).empty());
    assert(BbTrimCompletedBuffers(ticks, handles, 11, 32).size() == 64);
    assert(ticks.size() == 32);
    assert(BbTrimCompletedBuffers(ticks, handles, 11, 32).empty());
    assert(BbTrimCompletedBuffers(ticks, handles, 11, 0).size() == 32);
    assert(ticks.empty() && handles.empty());
    assert(BbTrimCompletedBuffers(ticks, handles, 11, 0).empty());
    std::puts("Command pool trimming: PASS (in-flight safety, completion boundary, reuse, empty pool)");
}
