// SPDX-License-Identifier: MIT
// Red-control linkage shim: the current header calls this helper for L1 shrink.
// Deliberately retain the old advisory behavior, not the fixed implementation.
#include "Interface/Core/LookupCache.h"
#include <FEXCore/Utils/Allocator.h>
void FEXCore::LookupCache::ResetAnonymousStorage(void* address,size_t size) {
 FEXCore::Allocator::VirtualDontNeed(address,size,false);
}
// The new header declares an out-of-line map destructor. Keep this old lookup
// reset control self-contained so the linker does not pull the current lookup
// implementation into the same executable. It is not the PMR leak red control.
FEXCore::GuestToHostMap::~GuestToHostMap() {}
