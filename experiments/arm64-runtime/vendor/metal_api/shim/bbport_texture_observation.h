// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
namespace BbTextureObservation {
// Observe the actual hit set by reference, never the cleared cache-miss output slot.
// Validate all dimensions before callbacks; this helper allocates/mutates nothing.
template<class Set,class Views,class Observer>
bool MemoHit(const Set& hit,const Views& views,Observer&& observe) {
    if(hit.count>hit.entries.size()||views.size()!=hit.count)return false;
    for(std::size_t i=0;i<views.size();++i)observe(i,hit.entries[i],views[i]);
    return true;
}
}
