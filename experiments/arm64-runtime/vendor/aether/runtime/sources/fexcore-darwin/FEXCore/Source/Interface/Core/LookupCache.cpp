#include "bb_fex_map_audit.h"
// SPDX-License-Identifier: MIT
/*
$info$
tags: glue|block-database
desc: Stores information about blocks, and provides C++ implementations to lookup the blocks
$end_info$
*/

#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/HLE/SyscallHandler.h>

#include "Interface/Context/Context.h"
#include "Interface/Core/LookupCache.h"
#ifdef __APPLE__
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace FEXCore {
void LookupCache::ResetAnonymousStorage(void* Address, size_t Size) {
#ifdef __APPLE__
  // MADV_DONTNEED on Darwin is advisory and retains bytes. Replace only our own
  // anonymous lookup pages at the same addresses: demand-zero without faulting in
  // the entire table. Never use this on JIT, shared guest or file-backed storage.
  const auto HostPage = sysconf(_SC_PAGESIZE);
  if (HostPage <= 0) {
    ERROR_AND_DIE_FMT("Host page size unavailable");
  }
  const auto Page = static_cast<uintptr_t>(HostPage);
  const auto Begin = reinterpret_cast<uintptr_t>(Address);
  const auto OwnedBegin = PagePointer;
  if (!Page || Size == 0 || Begin % Page || Size % Page || Begin < OwnedBegin ||
      Size > TotalCacheSize || Begin - OwnedBegin > TotalCacheSize - Size) {
    ERROR_AND_DIE_FMT("Invalid anonymous lookup reset range");
  }
  void* Result = ::mmap(Address, Size, PROT_READ | PROT_WRITE,
                        MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (Result != Address) {
    ERROR_AND_DIE_FMT("Anonymous lookup reset failed");
  }
#else
  FEXCore::Allocator::VirtualDontNeed(Address, Size, false);
#endif
}

GuestToHostMap::GuestToHostMap()
  : BlockLinks_mbr {"FEXMem_BlockLinks"} {
  BlockLinks_pma = fextl::make_unique<std::pmr::polymorphic_allocator<std::byte>>(&BlockLinks_mbr);
  // Setup our PMR map.
  BlockLinks = BlockLinks_pma->new_object<BlockLinksMapType>();
}

GuestToHostMap::~GuestToHostMap() {
  if (BlockLinks) BlockLinks_pma->delete_object(BlockLinks);
  // BlockLinks_mbr releases its backing pages after all map objects are destroyed.
}

LookupCache::LookupCache(FEXCore::Context::ContextImpl* CTX)
  : ctx {CTX} {

  TotalCacheSize = ctx->Config.VirtualMemSize / FEXCore::Utils::FEX_PAGE_SIZE * 8 + CODE_SIZE + MAX_L1_SIZE;

  // Block cache ends up looking like this
  // PageMemoryMap[VirtualMemoryRegion >> 12]
  //       |
  //       v
  // PageMemory[Memory & (VIRTUAL_PAGE_SIZE - 1)]
  //       |
  //       v
  // Pointer to Code
  //
  // Allocate a region of memory that we can use to back our block pointers
  // We need one pointer per page of virtual memory
  // At 64GB of virtual memory this will allocate 128MB of virtual memory space
  PagePointer = reinterpret_cast<uintptr_t>(FEXCore::Allocator::VirtualAlloc(TotalCacheSize, false, false));
  LOGMAN_THROW_A_FMT(PagePointer != -1ULL, "Failed to allocate PagePointer");

  // Disable THP on the Lookup cache.
  FEXCore::Allocator::VirtualTHPControl(reinterpret_cast<const void*>(PagePointer), TotalCacheSize, FEXCore::Allocator::THPControl::Disable);

  FEXCore::Allocator::VirtualName("FEXMem_Lookup", reinterpret_cast<void*>(PagePointer),
                                  ctx->Config.VirtualMemSize / FEXCore::Utils::FEX_PAGE_SIZE * 8 + CODE_SIZE);
  CTX->SyscallHandler->MarkOvercommitRange(PagePointer, TotalCacheSize);

  // Allocate our memory backing our pages
  // We need 32KB per guest page (One pointer per byte)
  // XXX: We can drop down to 16KB if we store 4byte offsets from the code base
  // We currently limit to 128MB of real memory for caching for the total cache size.
  // Can end up being inefficient if we compile a small number of blocks per page
  PageMemory = PagePointer + ctx->Config.VirtualMemSize / FEXCore::Utils::FEX_PAGE_SIZE * 8;

  // L1 Cache
  L1Pointer = PageMemory + CODE_SIZE;
  FEXCore::Allocator::VirtualName("FEXMem_Lookup_L1", reinterpret_cast<void*>(L1Pointer), MAX_L1_SIZE);

  VirtualMemSize = ctx->Config.VirtualMemSize;

  if (DynamicL1Cache()) {
    // Start at minimum size when dynamic.
    L1PointerMask = MIN_L1_ENTRIES - 1;
  } else {
    // Start at maximum instead.
    L1PointerMask = MAX_L1_ENTRIES - 1;
  }
  BbFexMap::Event('A',BbFexMap::Lookup,reinterpret_cast<void*>(PagePointer),TotalCacheSize);
}

LookupCache::~LookupCache() {
  BbFexMap::Event('F',BbFexMap::Lookup,reinterpret_cast<void*>(PagePointer),TotalCacheSize);
  FEXCore::Allocator::VirtualFree(reinterpret_cast<void*>(PagePointer), TotalCacheSize);
  ctx->SyscallHandler->UnmarkOvercommitRange(PagePointer, TotalCacheSize);

  // No need to free BlockLinks map.
  // These will get freed when their memory allocators are deallocated.
}

void LookupCache::ClearL2Cache(const FEXCore::LookupCacheBaseLockToken& lk) {
  // Clear out the page memory
  // PagePointer and PageMemory are sequential with each other. Clear both at once.
  ResetAnonymousStorage(reinterpret_cast<void*>(PagePointer),
                       ctx->Config.VirtualMemSize / FEXCore::Utils::FEX_PAGE_SIZE * 8 + CODE_SIZE);
  AllocateOffset = 0;
}

void LookupCache::ClearThreadLocalCaches(const LookupCacheWriteLockToken&) {
  // TODO: Preserve code cache entries?
  // Clear L1 and L2 by clearing the full cache.
  ResetAnonymousStorage(reinterpret_cast<void*>(PagePointer), TotalCacheSize);

  // TODO: Rename this member to avoid confusion with code caching
  CachedCodePages.clear();
}

void LookupCache::ClearCache(const LookupCacheWriteLockToken& lk) {
  // Clear L1 and L2 by clearing the full cache.
  ClearThreadLocalCaches(lk);
  Shared->ClearCache(lk);
}

void GuestToHostMap::ClearCache(const LookupCacheWriteLockToken&) {
  // Sever every recorded direct-branch link before discarding them. Erase(Address, ...) already
  // does this correctly for a single address (walk BlockLinks, call each delinker with its
  // HostLink), but until now this full-cache clear just replaced BlockLinks with a fresh empty
  // map, silently dropping every entry without ever calling its delinker. On every platform that
  // only ever grows into fresh memory on eviction (NewCodeBuffer=true, the normal case
  // everywhere) that gap was invisible: the stale patched branches point at addresses inside the
  // now-abandoned old buffer, which nothing valid ever branches back into again anyway. It
  // stopped being invisible on iOS, where JIT buffer reuse-in-place (NewCodeBuffer=false) was
  // tried as a workaround for a separate StikDebug allocation-count limitation: reusing the same
  // memory for new compiled code, with old delinkers never having been called, left other
  // still-referenced code's already-patched direct branches pointing at addresses that now held
  // completely unrelated new code. HandleGuestSignal's SIGBUS-recovery paths could keep
  // "fixing" the resulting faults' host/writable-vs-executable address one instruction at a
  // time, but never the actual stale target underneath -- confirmed on-device as an unbounded
  // fault loop (7,000,000+ recoveries cycling through only 8 distinct addresses, no crash, no
  // progress). Actually severing every link here, the same way Erase() already does for one
  // address, is the real fix for that, independent of whichever eviction mode ends up used.
  for (auto& [Tag, Delinker] : *BlockLinks) {
    Delinker(Tag.HostLink);
  }

  // All links were delinked under the caller's write lock. The old map and
  // invalidated nodes must die before their monotonic backing can be returned.
  // This changes no code-cache capacity, eviction threshold or live code owner.
  BlockLinks_pma->delete_object(BlockLinks);
  BlockLinks = nullptr;
  BlockLinks_mbr.release();
  BlockLinks = BlockLinks_pma->new_object<BlockLinksMapType>();
  // All code is gone, clear the block list
  BlockList.clear();
}

} // namespace FEXCore
