// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <atomic>
#include <string_view>

namespace Metal {
inline bool VertexMathInliningEnabled() {
    static const bool enabled=[] {const char* v=std::getenv("BB_MTL_VERTEX_MATH");return !v || v[0]!='0';}();
    return enabled;
}
inline bool VertexMathReferenceRequested(uint64_t hash) {
    const char* file=std::getenv("BB_MTL_VERTEX_MATH_FILE");
    return VertexMathInliningEnabled() && file && *file &&
        (hash==0xb10c4f55ull || hash==0x3deaa573ull || hash==0xadf9e624ull);
}
inline bool ReserveVertexMathReference() {
    static std::atomic<unsigned> created{0};
    const auto slot=created.fetch_add(1,std::memory_order_relaxed);
    if(slot==32) std::puts("Metal vertex math shader: reference capacity reached; optimized retained");
    return slot<32;
}
// Diagnostic selector is called only for a pipeline with a prebuilt reference state.
// Normal launches have no alternate libraries, PSOs or polling.
inline bool VertexMathOptimizedSelection() noexcept {
    static const char* file=std::getenv("BB_MTL_VERTEX_MATH_FILE");
    if(!file || !*file) return true;
    static std::atomic<bool> enabled{true};
    static std::atomic<int64_t> next{0};
    const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto deadline=next.load(std::memory_order_relaxed);
    if(now>=deadline && next.compare_exchange_strong(deadline,now+1000000000ll,std::memory_order_relaxed)) {
        if(FILE* f=std::fopen(file,"r")) {
            const int v=std::fgetc(f);std::fclose(f);
            if(v=='0'||v=='1') {
                const bool selected=v=='1';
                if(enabled.exchange(selected,std::memory_order_relaxed)!=selected)
                    std::printf("Metal vertex math mode: enabled=%d; prebuilt reference/inline main color PSOs\n",int(selected));
            }
        }
    }
    return enabled.load(std::memory_order_relaxed);
}

// Keep each existing explicit fma and its rounding boundary. Only remove the call barrier;
// Safe math and contract(off) remain mandatory. Do not touch matrix/other optnone helpers.
inline unsigned InlineStrictVertexMath(std::string& source) {
    unsigned replaced=0;
    for (const auto name : {"spvFAdd", "spvFSub", "spvFMul"}) {
        const std::string expression = std::string_view{name}=="spvFAdd" ? "fma(T(1), l, r)" :
            std::string_view{name}=="spvFSub" ? "fma(T(-1), r, l)" : "fma(l, r, T(0))";
        const std::string old="[[clang::optnone]] T "+std::string{name}+"(T l, T r)\n{\n    return "+expression+";\n}";
        const auto at=source.find(old);
        if(at==std::string::npos) continue;
        const std::string next="[[clang::always_inline]] inline T "+std::string{name}+"(T l, T r)\n{\n#pragma METAL fp math_mode(safe)\n#pragma METAL fp contract(off)\n    return "+expression+";\n}";
        source.replace(at,old.size(),next);++replaced;
    }
    return replaced;
}
} // namespace Metal
