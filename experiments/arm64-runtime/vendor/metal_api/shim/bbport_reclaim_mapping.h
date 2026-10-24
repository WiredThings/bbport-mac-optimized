// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
// v2 requires first-map history; old runtimes without this proof fail closed.
#if defined(__APPLE__)
extern "C" int runtime_memory_reclaim_if_unaliased_v2(uintptr_t,uint64_t,int(*)(void*),void*) __attribute__((weak_import));
#elif !defined(_WIN32)
extern "C" int runtime_memory_reclaim_if_unaliased_v2(uintptr_t,uint64_t,int(*)(void*),void*) __attribute__((weak));
#endif
namespace BbReclaimMapping {
inline bool WithUnaliasedMapping(uintptr_t a,uint64_t n,int(*callback)(void*),void* context) {
#if defined(_WIN32)
    (void)a; (void)n; (void)callback; (void)context;
    return false;
#else
    return runtime_memory_reclaim_if_unaliased_v2 && runtime_memory_reclaim_if_unaliased_v2(a,n,callback,context);
#endif
}
}
