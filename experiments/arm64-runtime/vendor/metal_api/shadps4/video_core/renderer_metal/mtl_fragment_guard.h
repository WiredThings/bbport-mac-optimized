// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace Metal {
// Bounded access metadata: canonical native buffers, not cache-wrapper identities.
// Collected for capture or the opt-in in-pass update candidate; never deletes a write.
// Bounding intervals may over-report conflicts; capacity/unknown metadata never permit one.
class FragmentAccessGuard {
public:
    using U64=std::uint64_t;
    enum Reason : unsigned { MissingDraw=1, RawAddress=2, UnknownTexture=4,
                             Capacity=8, InvalidRange=16 };
    struct Snapshot { U64 draws{},known_draws{};unsigned resources{},reasons{};bool overlap{}; };
    void Reset() { *this={}; }
    void Unknown(unsigned reason) { reasons|=reason; }
    void Ready() { ready=true; }
    void Draw() { ++draws;if(ready)++known_draws;else Unknown(MissingDraw);ready=false; }
    void Buffer(std::uintptr_t native,U64 offset,U64 size) {
        if(!Valid(native,offset,size)){Unknown(InvalidRange);return;}
        for(unsigned i=0;i<used;++i)if(ranges[i].native==native) {
            ranges[i].begin=std::min(ranges[i].begin,offset);
            ranges[i].end=std::max(ranges[i].end,offset+size);return;
        }
        if(used==ranges.size()){Unknown(Capacity);return;}
        ranges[used++]={native,offset,offset+size};
    }
    Snapshot Inspect(std::uintptr_t native,U64 offset,U64 size) const {
        Snapshot s{draws,known_draws,used,reasons,false};
        if(!Valid(native,offset,size)){s.reasons|=InvalidRange;return s;}
        for(unsigned i=0;i<used;++i)if(ranges[i].native==native &&
            offset<ranges[i].end && ranges[i].begin<offset+size)s.overlap=true;
        return s;
    }
private:
    static bool Valid(std::uintptr_t native,U64 offset,U64 size) {
        return native && size && size<=std::numeric_limits<U64>::max()-offset;
    }
    struct Range {std::uintptr_t native{};U64 begin{},end{};};
    std::array<Range,64> ranges{};
    unsigned used{},reasons{};
    U64 draws{},known_draws{};
    bool ready{};
};
static_assert(sizeof(FragmentAccessGuard)<2048);
}
