// SPDX-License-Identifier: GPL-2.0-or-later
// Network diagnostic reads must obey the runtime's guest mapping checks.
#pragma once
#include "common/types.h"
#include "common/singleton.h"
extern "C" int runtime_memory_is_mapped(uintptr_t address, uint64_t size);
namespace Core {
class MemoryManager {
public:
    bool IsValidMapping(VAddr address, u64 size) const {
        return size && address <= UINTPTR_MAX - size && runtime_memory_is_mapped(address, size);
    }
};
using Memory = Common::Singleton<MemoryManager>;
}
