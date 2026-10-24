// SPDX-License-Identifier: MIT

#include "hle_call_adapter.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace Core::GuestCpu {

std::shared_ptr<HleCallAdapter> HleCallRegistry::Register(std::shared_ptr<HleCallAdapter> adapter,
                                                           std::string_view name) {
    std::unique_lock lock{registry_mutex};
    if (adapter == nullptr || next_operation == 0) {
        return {};
    }
    const u64 operation=next_operation;
    const bool publish=fast_lookup && operation-1<FastPageCount*FastPageEntries;
    std::unique_ptr<PublishedPage> fresh;
    PublishedPage* page=nullptr;
    if(publish) {
        page=published_pages[(operation-1)/FastPageEntries].load(std::memory_order_relaxed);
        if(!page){fresh=std::make_unique<PublishedPage>();page=fresh.get();}
    }
    // Allocate/assign before publication. A failed allocation leaves no readable slot.
    adapter->AssignOperation(operation, name);
    adapters.emplace_back(adapter);
    ++next_operation;
    if(publish) {
        page->entries[(operation-1)%FastPageEntries].store(adapter.get(),std::memory_order_release);
        if(fresh)published_pages[(operation-1)/FastPageEntries].store(fresh.release(),std::memory_order_release);
    }
    return adapter;
}

HleCallRegistry::~HleCallRegistry() {
    // Same lifetime rule as the original registry reference: no live lookups now.
    for(auto& page:published_pages)delete page.load(std::memory_order_relaxed);
}

HleCallAdapter* HleCallRegistry::FindBorrowed(u64 operation) const {
    if(fast_lookup && operation && operation-1<FastPageCount*FastPageEntries) {
        auto* page=published_pages[(operation-1)/FastPageEntries].load(std::memory_order_acquire);
        return page?page->entries[(operation-1)%FastPageEntries].load(std::memory_order_acquire):nullptr;
    }
    // The original vector remains the sole owner and is never erased mid-run.
    // The temporary shared_ptr may drop: its entry still owns the returned pointer.
    return Find(operation).get();
}

std::shared_ptr<HleCallAdapter> HleCallRegistry::Find(u64 operation) const {
    std::shared_lock lock{registry_mutex};
    if (operation == 0 || operation > adapters.size()) {
        return {};
    }
    return adapters.at(operation - 1);
}

HleVeneerAllocator::~HleVeneerAllocator() {
    for (const auto& allocation : allocations) {
        if (allocation.page != nullptr && munmap(allocation.page, allocation.size) != 0) {
            std::abort();
        }
    }
}

const char* HleVeneerAllocator::LayoutName() const noexcept {
#if defined(__APPLE__) && TARGET_OS_IPHONE
    return "ios-dual-rx-v1";
#elif defined(__APPLE__)
    return packed_veneers?"darwin-packed-guest-ro-v1":"standalone-native-rx-v1";
#else
    return "standalone-native-rx-v1";
#endif
}

HleVeneerResult HleVeneerAllocator::Allocate(const HleCallAdapter& adapter) {
    std::scoped_lock lock{allocator_mutex};
    if (adapter.Operation() == 0) {
        return HleVeneerFailure{EINVAL};
    }
    if (const auto cached = veneers.find(adapter.Operation()); cached != veneers.end()) {
        return cached->second;
    }

    constexpr std::size_t veneer_size = 16;
    u8 code[veneer_size];
    // mov r10, rcx preserves the fourth SysV argument across syscall's RCX clobber.
    // mov rax, operation; syscall; ret
    constexpr u8 preserve_fourth_argument[]{0x49, 0x89, 0xca};
    constexpr u8 prefix[]{0x48, 0xb8};
    constexpr u8 suffix[]{0x0f, 0x05, 0xc3};
    std::memcpy(code, preserve_fourth_argument, sizeof(preserve_fourth_argument));
    std::memcpy(code + sizeof(preserve_fourth_argument), prefix, sizeof(prefix));
    const auto operation = adapter.Operation();
    const auto operation_offset = sizeof(preserve_fourth_argument) + sizeof(prefix);
    std::memcpy(code + operation_offset, &operation, sizeof(operation));
    std::memcpy(code + operation_offset + sizeof(operation), suffix, sizeof(suffix));

    u64 address;
#if defined(__APPLE__) && TARGET_OS_IPHONE
    // See VeneerBatch's own doc comment (hle_call_adapter.h) for why this batches into a
    // shared dual-mapped region instead of one mmap+mprotect page per veneer.
    constexpr std::size_t kVeneerBatchSize = 16 * 1024;
    if (batches.empty() || batches.back().used + veneer_size > batches.back().region.size) {
        auto region = Core::DualMappedRegion::Allocate(kVeneerBatchSize);
        if (!region.IsValid()) {
            return HleVeneerFailure{ENOMEM};
        }
        executable_ranges.push_back(
            {reinterpret_cast<std::uintptr_t>(region.rx_addr), region.size, true, false});
        batches.push_back(VeneerBatch{std::move(region), 0});
    }
    auto& batch = batches.back();
    auto* const write_ptr = batch.region.rw_addr + batch.used;
    auto* const exec_ptr = batch.region.rx_addr + batch.used;
    std::memcpy(write_ptr, code, veneer_size);
    // Both aliases need their own cache maintenance: dcache was dirtied at write_ptr, icache
    // is what actually gets fetched from at exec_ptr (see ios_jit_allocator.h's own comments
    // on this same requirement elsewhere in the codebase).
    __builtin___clear_cache(reinterpret_cast<char*>(write_ptr),
                            reinterpret_cast<char*>(write_ptr + veneer_size));
    __builtin___clear_cache(reinterpret_cast<char*>(exec_ptr),
                            reinterpret_cast<char*>(exec_ptr + veneer_size));
    address = reinterpret_cast<u64>(exec_ptr);
    batch.used += veneer_size;
#else
#if defined(__APPLE__)
    if(packed_veneers) {
        if(guest_slabs.empty()||guest_slabs.back().used+veneer_size>guest_slabs.back().region->Size()) {
            // Reserve metadata before allocating VM. An allocation failure cannot
            // strand either alias or publish a partial executable range.
            guest_slabs.reserve(guest_slabs.size()+1);
            executable_ranges.reserve(executable_ranges.size()+1);
            auto made=BbDarwinVeneer::Slab::Create(16384);
            if(auto* error=std::get_if<int>(&made))return HleVeneerFailure{*error};
            auto region=std::move(std::get<std::unique_ptr<BbDarwinVeneer::Slab>>(made));
            const auto begin=reinterpret_cast<uintptr_t>(region->Reader()),size=region->Size();
            guest_slabs.push_back({std::move(region),0});
            executable_ranges.push_back({begin,size,true,false});
        }
        auto& slab=guest_slabs.back();
        // Earlier slots never change, even while other FEX threads execute.
        std::memcpy(slab.region->Writer()+slab.used,code,veneer_size);
        address=reinterpret_cast<uint64_t>(slab.region->Reader()+slab.used);
        slab.used+=veneer_size;
    } else
#endif
    {
    const auto page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return HleVeneerFailure{errno == 0 ? EIO : errno};
    }
    const auto size = static_cast<std::size_t>(page_size);
    void* const page = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        return HleVeneerFailure{errno};
    }
    std::memcpy(page, code, veneer_size);
    __builtin___clear_cache(reinterpret_cast<char*>(page), reinterpret_cast<char*>(page) + veneer_size);
    if (mprotect(page, size, PROT_READ | PROT_EXEC) != 0) {
        const int error = errno;
        if (munmap(page, size) != 0) {
            std::abort();
        }
        return HleVeneerFailure{error};
    }
    allocations.emplace_back(page, size);
    executable_ranges.push_back({reinterpret_cast<std::uintptr_t>(page), size, true, false});
    address = reinterpret_cast<u64>(page);
    }
#endif

    std::fprintf(stderr, "BACHATA_FEX_VENEER address=%#llx operation=%llu name=%.*s\n",
                 static_cast<unsigned long long>(address),
                 static_cast<unsigned long long>(adapter.Operation()),
                 static_cast<int>(adapter.Name().size()), adapter.Name().data());
    veneers.emplace(adapter.Operation(), address);
    return address;
}

std::vector<Core::GuestExecutionRange> HleVeneerAllocator::GetExecutableRanges() const {
    std::scoped_lock lock{allocator_mutex};
    return executable_ranges;
}

std::optional<Core::GuestExecutionRange> HleVeneerAllocator::QueryExecutableRange(
    std::uintptr_t address) const {
    if (address == 0) {
        return std::nullopt;
    }
    std::scoped_lock lock{allocator_mutex};
    for (const auto& range : executable_ranges) {
        if (!range.Executable || range.Begin == 0 || range.Size == 0) {
            continue;
        }
        if (address >= range.Begin && address < range.Begin + range.Size) {
            return range;
        }
    }
    return std::nullopt;
}

} // namespace Core::GuestCpu
