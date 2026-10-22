// SPDX-License-Identifier: GPL-2.0-or-later
// Old runtimes without the new mapping proof must never execute a free callback.
#include "bbport_reclaim_mapping.h"
#include <cassert>
#include <cstdio>
static unsigned old_called;
// A deployed v1 proving only current VMAs must not satisfy the v2 weak import.
extern "C" int runtime_memory_reclaim_if_unaliased(uintptr_t,uint64_t,int(*callback)(void*),void* context) {
    ++old_called;return callback(context);
}
int main() {
    unsigned called=0;
    const bool removed=BbReclaimMapping::WithUnaliasedMapping(0x7000000000ull,16384,
        [](void* p){++*static_cast<unsigned*>(p);return 1;},&called);
    assert(!removed && called==0 && old_called==0);
    std::puts("Absent v2 mapping API: old v1 ignored, callback not called, retirement refused");
}
