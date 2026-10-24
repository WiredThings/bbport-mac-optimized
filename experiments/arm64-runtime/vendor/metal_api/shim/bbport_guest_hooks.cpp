// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_guest_hooks.h"
#include "remote/bb_remote.h"
#include "bbport_heap_sites.h"
#include "bbport_resource_lifetime.h"
#include "bbport_toggles.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <span>
#include <sys/mman.h>
#include <sys/uio.h>
#include <ucontext.h>
#include <unistd.h>

extern "C" void runtime_memory_note_write(uintptr_t address, uint64_t size);
extern "C" int runtime_memory_vma_info(uintptr_t,int*,int*,uintptr_t*);
extern "C" const uint64_t* runtime_memory_generation();

namespace BbGuestHooks {
namespace {
using u64 = std::uint64_t;
using u8 = std::uint8_t;
constexpr u64 ImageBase = 0x800000000ull;

/// The return of the game's GPU memory range allocator 0x26aa070 (rdi allocator, rsi size,
/// rdx alignment; it returns a node: [node + 8] the start, [[node + 0x18] + 8] the end, the next
/// node's start): `add rsp, 0x18` in its epilogue, reached by its own branches only.
constexpr u64 AllocReturn = 0x26aa255;
constexpr unsigned char AllocReturnCode[4] = {0x48, 0x83, 0xc4, 0x18};
RangeCallback on_range_allocated = nullptr;
std::atomic<bool> fex_detours_complete{false};
std::atomic<bool> fex_callback_ready{false};
std::atomic<RangeCallback> fex_on_range{nullptr};
std::mutex fex_configuration_mutex;

/// The game's other GPU memory heap (TLSF-like, headers in the memory: free at 0x20858a0, alloc at
/// 0x2085a60), reached through a pointer at 0x54b9f38 set at startup. Its only three callers
/// (wrappers that record {address, size} in a map) pass r8 = rbp - 0x50 for the size handed out and
/// follow the call with `mov rbx, rax` (rax: the address).
constexpr std::array<u64, 3> HeapAllocReturns = {0x11e6629, 0x11e6736, 0x11e6846};
constexpr unsigned char HeapAllocReturnCode[3] = {0x48, 0x89, 0xc3};

/// BB_FRAME_STATS: the range allocator's collector (0x26aa860, `push rbp`; rdi the allocator): its
/// live range count [+0xc8] and byte count [+0xd0] are noted for the frame stats.
constexpr u64 CollectorEntry = 0x26aa860;
constexpr unsigned char CollectorEntryCode[1] = {0x55};

/// BB_HEAP_SITES=1: the game's deferred object release (0xbb5f10) skips the destruction when the
/// registry check (0xbf0790) fails: `js 0xbb5fc7` there, its result counted (eax).
constexpr u64 ReleaseCheck = 0xbb5fb0;
constexpr unsigned char ReleaseCheckCode[2] = {0x78, 0x15};

/// `call memcpy` (0x261a1b0: rdi destination, rsi source, rdx size) in the resource loaders:
/// 0x216d420 and 0x2171d60 copy texture and buffer data into GPU memory (most of the write
/// faults while areas stream in).
struct CallSite {
    u64 offset;
    unsigned char code[5];
};
constexpr std::array<CallSite, 2> MemcpySites = {{
    {0x216d5ea, {0xe8, 0xc1, 0xcb, 0x4a, 0x00}},
    {0x2171fea, {0xe8, 0xc1, 0x81, 0x4a, 0x00}},
}};
std::array<std::atomic<u64>, MemcpySites.size()> hits{};

/// The game's memcpy at those sites: the GPU side is told before (its write traps lift: no fault
/// per page) and after the copy (the data now there), as for file reads.
void* LoaderCopy(void* dst, const void* src, std::size_t size) {
    runtime_memory_note_write(reinterpret_cast<u64>(dst), size);
    std::memcpy(dst, src, size);
    runtime_memory_note_write(reinterpret_cast<u64>(dst), size);
    return dst;
}

/// bbport: native detours (default; BB_GUEST_HOOK_TRAPS=1: int3 traps). Each hooked site jumps to
/// a stub next to the image that calls the handler and runs the instructions the jump covers;
/// the copy call sites call LoaderCopy through a trampoline there. An int3 trap costs a signal
/// (a few microseconds in the kernel) per call; a detour costs a call. The int3 written first
/// stays the fallback while the jump's bytes go in: OnTrap sends a thread that meets it to the stub.
struct NativeSite {
    u64 offset;
    std::atomic<u64> stub{0};
};
constexpr u64 RangeFree = 0x26aa986, HeapFree = 0x20858a0;
constexpr u64 HeapPlainAlloc = 0x2085730, HeapAlignedAlloc = 0x2085a60;
constexpr u8 HeapPlainCode[] = {0x53,0x31,0xc0,0x48,0x39,0x77,0x18};
constexpr u8 HeapAlignedCode[] = {0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54};
using HeapPlainFn = u64 (*)(u64,u64);
using HeapAlignedFn = u64 (*)(u64,u64,u64);
HeapPlainFn original_heap_plain{};
HeapAlignedFn original_heap_aligned{};
bool heap_entry_hooks = false;
thread_local unsigned heap_alloc_depth{};
struct HeapWatch {u64 address{},size{};};
HeapWatch ParseHeapWatch(const char* input,bool diagnostic) {
    if(!diagnostic || !input)return {};
    char* end=nullptr;const u64 address=std::strtoull(input,&end,0);
    const u64 size=end && *end==','?std::strtoull(end+1,&end,0):0;
    if(!end || *end || !address || address>=(u64{1}<<40) || size!=192 ||
        size>(u64{1}<<40)-address)return {};
    return {address,size};
}
bool CoversHeapWatch(HeapWatch watch,u64 address,u64 size) {
    return watch.size && address && address<=watch.address && size>=watch.size &&
        watch.address-address<=size-watch.size;
}
std::atomic<unsigned> heap_watch_records{0};
struct HeapCallers {std::array<u64,4> addresses{};unsigned count{};};
bool ReadHeapFrame(u64 address,std::array<u64,2>& values) {
    iovec local{values.data(),sizeof(values)},remote{reinterpret_cast<void*>(address),sizeof(values)};
    return process_vm_readv(getpid(),&local,1,&remote,1,0)==ssize_t(sizeof(values));
}
HeapCallers GatherHeapCallers(u64 frame,u64 expected,
                              bool (*read)(u64,std::array<u64,2>&)=&ReadHeapFrame) {
    HeapCallers result;
    for(unsigned depth=0;depth<result.addresses.size();++depth) {
        // At most four readable frame records; no raw dereference or stack scan.
        // A missing/omitted frame pointer terminates the chain. The first return
        // must match the compiler-provided direct caller before following links.
        if(frame<4096 || (frame&7) || frame>~u64{0}-16)break;
        std::array<u64,2> values{};
        if(!read(frame,values) || !values[1] || (!depth && values[1]!=expected))break;
        result.addresses[result.count++]=values[1];
        if(values[0]<=frame || values[0]-frame>65536)break;
        frame=values[0];
    }
    return result;
}
void TraceWatchedHeap(const char* action,const char* kind,BbResourceLifetime::Tag tag,
                      u64 alignment,u64 caller,u64 frame,bool accepted=true) {
    static const HeapWatch watch=[] {
        const char* flag=std::getenv("BB_CAPTURE_NATIVE");
        return ParseHeapWatch(std::getenv("BB_WATCH"),flag && flag[0]=='1');
    }();
    if(!CoversHeapWatch(watch,tag.address,tag.size))return;
    if(heap_watch_records.fetch_add(1,std::memory_order_relaxed)>=16)return;
    // Only the opted-in watched allocation reaches the bounded frame reader.
    // Return addresses are metadata, not an interpretation of resource payloads;
    // the offline ELF map still has to establish executable-code membership.
    const auto callers=GatherHeapCallers(frame,caller);
    std::printf("Watched heap: {\"action\":\"%s\",\"kind\":\"%s\",\"address\":%llu,\"bytes\":%llu,\"generation\":%llu,\"alignment\":%llu,\"caller\":%llu,\"image_base\":%llu,\"accepted\":%s,\"caller_chain\":[%llu,%llu,%llu,%llu],\"caller_count\":%u}\n",
        action,kind,(unsigned long long)tag.address,(unsigned long long)tag.size,
        (unsigned long long)tag.generation,(unsigned long long)alignment,
        (unsigned long long)caller,(unsigned long long)ImageBase,accepted?"true":"false",
        (unsigned long long)callers.addresses[0],(unsigned long long)callers.addresses[1],
        (unsigned long long)callers.addresses[2],(unsigned long long)callers.addresses[3],callers.count);
}
using ScopeQueryFn = int (*)(uintptr_t,int*,int*,uintptr_t*);
ScopeQueryFn scope_query=&runtime_memory_vma_info;
const uint64_t* (*generation_query)()=&runtime_memory_generation;

// General CPU heaps use these TLSF routines too. Exclude them before entering the
// tracking mutex. Cache only within a stable runtime mapping generation; remaps or
// protection changes invalidate every cached classification, including negative hits.
bool GpuScope(u64 address) {
    if (!address) return false;
    struct Scope { u64 start{},end{},generation{};bool gpu{}; };
    thread_local std::array<Scope,8> scopes{};
    thread_local unsigned next{};
    const auto* sequence=generation_query();
    const u64 generation=__atomic_load_n(sequence,__ATOMIC_ACQUIRE);
    if (!(generation&1)) {
        for(const auto& scope:scopes)
            if(scope.generation==generation && address>=scope.start && address<scope.end)
                return scope.gpu;
    }
    int prot=0,type=-1;uintptr_t end=address;
    const bool found=scope_query(address,&prot,&type,&end)!=0;
    const bool gpu=found && type>=0 && (prot&0x30)!=0;
    const u64 after=__atomic_load_n(sequence,__ATOMIC_ACQUIRE);
    if(generation==after && !(after&1) && end>address)
        scopes[next++%scopes.size()]={address,end,after,gpu};
    return gpu;
}
constexpr u8 RangeFreeCode[] = {0x48,0x8b,0x4c,0x24,0x28,0x48,0xff,0x89,0xc8,0,0,0};
constexpr u8 HeapFreeCode[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56};
using HeapFreeFn = void (*)(u64, u64);
HeapFreeFn original_heap_free{};
std::array<NativeSite, 8> native_sites{{{AllocReturn}, {HeapAllocReturns[0]}, {HeapAllocReturns[1]},
                                        {HeapAllocReturns[2]}, {RangeFree}, {HeapFree}, {HeapPlainAlloc}, {HeapAlignedAlloc}}};
u8* stub_page = nullptr;
std::size_t stub_used = 0;
constexpr std::size_t StubPageSize = 4096;

/// A page within +-2 GiB of the image (below its base: the image grows upwards).
u8* MapStubPage() {
    for (u64 k = 1; k <= 64; ++k) {
        const u64 hint = ImageBase - k * (16ull << 20);
        void* p = mmap(reinterpret_cast<void*>(hint), StubPageSize, PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p != MAP_FAILED) {
            return static_cast<u8*>(p);
        }
    }
    return nullptr;
}

struct Emitter {
    std::array<u8, 1024> code{};
    std::size_t size = 0;
    void Bytes(std::initializer_list<u8> bytes) {
        for (const u8 b : bytes) {
            code[size++] = b;
        }
    }
    void U32(std::uint32_t v) {
        std::memcpy(&code[size], &v, 4);
        size += 4;
    }
    void U64(u64 v) {
        std::memcpy(&code[size], &v, 8);
        size += 8;
    }
    /// Saves the caller-saved registers (the handler may change them), aligns the stack.
    void SaveVolatile() {
        Bytes({0x50, 0x51, 0x52, 0x56, 0x57, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53});
        Bytes({0x41, 0x54});             // push r12 (holds the unaligned stack pointer)
        Bytes({0x49, 0x89, 0xe4});       // mov r12, rsp
        Bytes({0x48, 0x83, 0xe4, 0xf0}); // and rsp, -16
    }
    // Inline collector branch: unlike a function-call boundary, every vector register,
    // flags, x87 state and MXCSR can be live. Keep the guest red zone untouched too.
    void SaveFull() {
        Bytes({0x48,0x8d,0xa4,0x24,0x80,0xff,0xff,0xff,0x9c});
        SaveVolatile();
        Bytes({0x48,0x81,0xec}); U32(1056);
        Bytes({0x48,0x0f,0xae,0x04,0x24}); // fxsave64 [rsp] (16-byte aligned)
        for (unsigned i=0;i<16;++i) {
            Bytes({0xc5, u8(i<8?0xfe:0x7e),0x7f,u8(0x84+8*(i&7)),0x24});
            U32(512+32*i); // vmovdqu [rsp+offset], ymmN (preserve upper AVX lanes)
        }
    }
    void RestoreFull() {
        Bytes({0x48,0x0f,0xae,0x0c,0x24}); // fxrstor64 [rsp]
        for (unsigned i=0;i<16;++i) {
            Bytes({0xc5, u8(i<8?0xfe:0x7e),0x6f,u8(0x84+8*(i&7)),0x24}); U32(512+32*i);
        }
        RestoreVolatile();
        Bytes({0x9d,0x48,0x8d,0xa4,0x24,0x80,0,0,0});
    }
    void CallAbs(const void* fn) {
        Bytes({0x48, 0xb8}); // movabs rax, fn
        U64(reinterpret_cast<u64>(fn));
        Bytes({0xff, 0xd0}); // call rax
    }
    void RestoreVolatile() {
        Bytes({0x4c, 0x89, 0xe4}); // mov rsp, r12
        Bytes({0x41, 0x5c});       // pop r12
        Bytes({0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58, 0x5f, 0x5e, 0x5a, 0x59, 0x58});
    }
};

/// Copies `e` into the stub page; returns its address there, or 0.
u64 PlaceStub(const Emitter& e) {
    if (!stub_page || stub_used + e.size > StubPageSize) {
        return 0;
    }
    u8* at = stub_page + stub_used;
    std::memcpy(at, e.code.data(), e.size);
    stub_used += (e.size + 15) & ~std::size_t(15);
    return reinterpret_cast<u64>(at);
}

bool Rel32To(u64 from_next, u64 to, std::uint32_t& rel) {
    const std::int64_t d = std::int64_t(to) - std::int64_t(from_next);
    if (d < INT32_MIN || d > INT32_MAX) {
        return false;
    }
    rel = std::uint32_t(std::int32_t(d));
    return true;
}

/// Replaces bytes [1, 5) at `address` (byte 0 is the trap already there), then byte 0.
void WriteJumpOverTrap(u64 address, u8 opcode, std::uint32_t rel) {
    const long page_size = sysconf(_SC_PAGESIZE);
    void* page = reinterpret_cast<void*>(address & ~u64(page_size - 1));
    const std::size_t span = (address + 5) - reinterpret_cast<u64>(page);
    mprotect(page, span, PROT_READ | PROT_WRITE | PROT_EXEC);
    auto* p = reinterpret_cast<u8*>(address);
    for (int i = 0; i < 4; ++i) {
        __atomic_store_n(p + 1 + i, u8(rel >> (8 * i)), __ATOMIC_SEQ_CST);
    }
    __atomic_store_n(p, opcode, __ATOMIC_SEQ_CST);
    mprotect(page, span, PROT_READ | PROT_EXEC);
}

void AllocHook(u64 node) {
    if (!node) {
        return;
    }
    const u64 start = *reinterpret_cast<const u64*>(node + 8);
    const u64 next = *reinterpret_cast<const u64*>(node + 0x18);
    const u64 end = next ? *reinterpret_cast<const u64*>(next + 8) : start;
    if (end > start && end - start < (u64(1) << 32)) {
        if (BbResourceLifetime::Enabled())
            BbResourceLifetime::Get().Allocate(BbResourceLifetime::Domain::Range, start, end-start);
        on_range_allocated(start, end - start);
    }
}

void HeapHook(u64 address, u64 size) {
    if (address != 0 && size != 0 && size < (u64(1) << 32)) {
        if (BbResourceLifetime::Enabled() && !heap_entry_hooks && GpuScope(address))
            BbResourceLifetime::Get().Allocate(BbResourceLifetime::Domain::Heap, address, size);
        on_range_allocated(address, size);
    }
}

// The collector has already accepted this node as free; capture before coalescing
// destroys its boundaries. It still holds the game's allocator lock at this branch.
void RangeFreeHook(u64 node) {
    const u64 start=*reinterpret_cast<const u64*>(node+8);
    const u64 next=*reinterpret_cast<const u64*>(node+0x18);
    const u64 end=next ? *reinterpret_cast<const u64*>(next+8) : start;
    auto tag=BbResourceLifetime::Get().Find(BbResourceLifetime::Domain::Range,start);
    if (!tag || end<=start || tag.size!=end-start)
        tag={start,0,0,BbResourceLifetime::Domain::Range};
    BbResourceLifetime::Get().Release(tag);
}
void HeapFreeHook(u64 allocator,u64 address) {
    if (!GpuScope(address)) {original_heap_free(allocator,address);return;}
    auto tag=BbResourceLifetime::Get().Find(BbResourceLifetime::Domain::Heap,address);
    if (!tag) tag={address,0,0,BbResourceLifetime::Domain::Heap};
    original_heap_free(allocator,address);
    const bool released=BbResourceLifetime::Get().Release(tag);
    TraceWatchedHeap("free","heap",tag,0,u64(__builtin_return_address(0)),
                     u64(__builtin_frame_address(0)),released);
}
u64 HeapPlainHook(u64 allocator,u64 size) {
    const bool top=heap_alloc_depth++==0;
    const u64 address=original_heap_plain(allocator,size);
    --heap_alloc_depth;
    if(top && address && GpuScope(address)) {
        const auto tag=BbResourceLifetime::Get().Allocate(BbResourceLifetime::Domain::Heap,address,size);
        TraceWatchedHeap("allocate","plain",tag,0,u64(__builtin_return_address(0)),
                         u64(__builtin_frame_address(0)));
    }
    return address;
}
u64 HeapAlignedHook(u64 allocator,u64 size,u64 alignment) {
    const bool top=heap_alloc_depth++==0;
    const u64 address=original_heap_aligned(allocator,size,alignment);
    --heap_alloc_depth;
    if(top && address && GpuScope(address)) {
        const auto tag=BbResourceLifetime::Get().Allocate(BbResourceLifetime::Domain::Heap,address,size);
        TraceWatchedHeap("allocate","aligned",tag,alignment,u64(__builtin_return_address(0)),
                         u64(__builtin_frame_address(0)));
    }
    return address;
}

/// The detour of a site whose int3 is in place (Patch). False: left as a trap.
bool InstallDetour(NativeSite& site) {
    const u64 address = ImageBase + site.offset;
    Emitter e;
    u64 back = 0;
    if (site.offset == AllocReturn) {
        // `add rsp, 0x18; pop rbx` (5 bytes) in the allocator's epilogue; rax: the node.
        if (reinterpret_cast<const u8*>(address)[4] != 0x5b) {
            return false;
        }
        e.SaveVolatile();
        e.Bytes({0x48, 0x89, 0xc7}); // mov rdi, rax (unchanged by the saving)
        e.CallAbs(reinterpret_cast<const void*>(&AllocHook));
        e.RestoreVolatile();
        e.Bytes({0x48, 0x83, 0xc4, 0x18}); // add rsp, 0x18
        e.Bytes({0x5b});                   // pop rbx
        back = address + 5;
    } else {
        // `mov rbx, rax; test rbx, rbx` (6 bytes) after the heap call; [rbp - 0x50]: the size.
        const auto* code = reinterpret_cast<const u8*>(address);
        if (code[3] != 0x48 || code[4] != 0x85 || code[5] != 0xdb) {
            return false;
        }
        e.Bytes({0x48, 0x89, 0xc3});                               // mov rbx, rax
        e.Bytes({0x48, 0x8d, 0xa4, 0x24, 0x80, 0xff, 0xff, 0xff}); // lea rsp, [rsp - 128]
        e.SaveVolatile();
        e.Bytes({0x48, 0x89, 0xdf});       // mov rdi, rbx
        e.Bytes({0x48, 0x8b, 0x75, 0xb0}); // mov rsi, [rbp - 0x50]
        e.CallAbs(reinterpret_cast<const void*>(&HeapHook));
        e.RestoreVolatile();
        e.Bytes({0x48, 0x8d, 0xa4, 0x24, 0x80, 0x00, 0x00, 0x00}); // lea rsp, [rsp + 128]
        e.Bytes({0x48, 0x85, 0xdb});                               // test rbx, rbx
        back = address + 6;
    }
    // jmp back
    const u64 jmp_at = reinterpret_cast<u64>(stub_page + stub_used) + e.size;
    std::uint32_t rel_back = 0;
    if (!Rel32To(jmp_at + 5, back, rel_back)) {
        return false;
    }
    e.Bytes({0xe9});
    e.U32(rel_back);
    const u64 stub = PlaceStub(e);
    std::uint32_t rel = 0;
    if (!stub || !Rel32To(address + 5, stub, rel)) {
        return false;
    }
    site.stub.store(stub, std::memory_order_release); // a thread at the trap now goes there
    WriteJumpOverTrap(address, 0xe9, rel);
    return true;
}

/// The copy call sites: the call's target becomes a trampoline to LoaderCopy.
bool InstallCallDetour(u64 offset) {
    Emitter e;
    e.Bytes({0xff, 0x25, 0x00, 0x00, 0x00, 0x00}); // jmp [rip]
    e.U64(reinterpret_cast<u64>(&LoaderCopy));
    const u64 stub = PlaceStub(e);
    const u64 address = ImageBase + offset;
    std::uint32_t rel = 0;
    if (!stub || !Rel32To(address + 5, stub, rel)) {
        return false;
    }
    WriteJumpOverTrap(address, 0xe8, rel);
    return true;
}
struct sigaction previous_action {};

void OnTrap(int sig, siginfo_t* info, void* context) {
    auto* g = BB_GREGS(static_cast<ucontext_t*>(context));
    const u64 rip = u64(g[REG_RIP]) - 1; // past the int3
    for (const auto& site : native_sites) {
        if (rip == ImageBase + site.offset) {
            if (const u64 stub = site.stub.load(std::memory_order_acquire)) {
                g[REG_RIP] = greg_t(stub); // the detour is going in: its stub runs the site
                return;
            }
        }
    }
    if (rip == ImageBase + ReleaseCheck) {
        const auto result = static_cast<std::int32_t>(g[REG_RAX] & 0xffffffff);
        BbHeapSites::NoteReleaseCheck(std::uint32_t(result));
        g[REG_RIP] = greg_t(ImageBase + (result < 0 ? 0xbb5fc7 : ReleaseCheck + 2));
        return;
    }
    if (rip == ImageBase + CollectorEntry) {
        const u64 allocator = u64(g[REG_RDI]);
        for (std::size_t i = 0; i < BbStats::range_allocators.size(); ++i) {
            u64 expected = 0;
            if (BbStats::range_allocators[i].load(std::memory_order_relaxed) == allocator ||
                BbStats::range_allocators[i].compare_exchange_strong(expected, allocator)) {
                BbStats::range_live[i].store(*reinterpret_cast<const u64*>(allocator + 0xc8),
                                             std::memory_order_relaxed);
                BbStats::range_bytes[i].store(*reinterpret_cast<const u64*>(allocator + 0xd0),
                                              std::memory_order_relaxed);
                break;
            }
        }
        g[REG_RSP] -= 8; // push rbp
        *reinterpret_cast<u64*>(g[REG_RSP]) = u64(g[REG_RBP]);
        g[REG_RIP] = greg_t(ImageBase + CollectorEntry + 1);
        return;
    }
    for (const u64 site : HeapAllocReturns) {
        if (rip != ImageBase + site) {
            continue;
        }
        HeapHook(u64(g[REG_RAX]), *reinterpret_cast<const u64*>(u64(g[REG_RBP])-0x50));
        g[REG_RBX] = g[REG_RAX];
        g[REG_RIP] = greg_t(ImageBase + site + sizeof(HeapAllocReturnCode));
        return;
    }
    if (rip == ImageBase + AllocReturn) {
        AllocHook(u64(g[REG_RAX]));
        g[REG_RSP] += 0x18;
        g[REG_RIP] = greg_t(ImageBase + AllocReturn + sizeof(AllocReturnCode));
        return;
    }
    for (std::size_t i = 0; i < MemcpySites.size(); ++i) {
        const auto& site = MemcpySites[i];
        if (rip != ImageBase + site.offset) {
            continue;
        }
        hits[i].fetch_add(1, std::memory_order_relaxed);
        // The call, to LoaderCopy instead of the game's memcpy: push the return address, jump.
        const u64 ret = ImageBase + site.offset + 5;
        g[REG_RSP] -= 8;
        *reinterpret_cast<u64*>(g[REG_RSP]) = ret;
        g[REG_RIP] = greg_t(reinterpret_cast<u64>(&LoaderCopy));
        return;
    }
    if (previous_action.sa_flags & SA_SIGINFO) {
        if (previous_action.sa_sigaction) {
            previous_action.sa_sigaction(sig, info, context);
            return;
        }
    } else if (previous_action.sa_handler != SIG_DFL && previous_action.sa_handler != SIG_IGN &&
               previous_action.sa_handler) {
        previous_action.sa_handler(sig);
        return;
    }
    signal(SIGTRAP, SIG_DFL);
    raise(SIGTRAP);
}

bool Patch(u64 address, const unsigned char* expected, std::size_t size) {
    unsigned char bytes[16]{};
    iovec local{bytes, size}, remote{reinterpret_cast<void*>(address), size};
    if (process_vm_readv(getpid(), &local, 1, &remote, 1, 0) != ssize_t(size) ||
        std::memcmp(bytes, expected, size) != 0) {
        return false;
    }
    const long page_size = sysconf(_SC_PAGESIZE);
    void* page = reinterpret_cast<void*>(address & ~u64(page_size - 1));
    const std::size_t span = (address + size) - reinterpret_cast<u64>(page);
    if (mprotect(page, span, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }
    __atomic_store_n(reinterpret_cast<unsigned char*>(address), static_cast<unsigned char>(0xcc),
                     __ATOMIC_SEQ_CST);
    mprotect(page, span, PROT_READ | PROT_EXEC);
    return true;
}
bool InstallLifetimeFree(NativeSite& site) {
    const u64 address=ImageBase+site.offset;
    Emitter e;
    if (site.offset==HeapFree || site.offset==HeapPlainAlloc || site.offset==HeapAlignedAlloc) {
        // Original prologue has no relative operands. The wrapper calls this trampoline,
        // rather than calling the patched entry recursively.
        unsigned prologue=0;
        const void* wrapper=nullptr;
        if(site.offset==HeapFree) {
            e.Bytes({0x55,0x48,0x89,0xe5,0x41,0x57});prologue=6;
            wrapper=reinterpret_cast<const void*>(&HeapFreeHook);
        } else if(site.offset==HeapPlainAlloc) {
            e.Bytes({0x53,0x31,0xc0,0x48,0x39,0x77,0x18});prologue=7;
            wrapper=reinterpret_cast<const void*>(&HeapPlainHook);
        } else {
            e.Bytes({0x41,0x57,0x41,0x56,0x41,0x55});prologue=6;
            wrapper=reinterpret_cast<const void*>(&HeapAlignedHook);
        }
        const u64 from=reinterpret_cast<u64>(stub_page+stub_used)+e.size;
        std::uint32_t rel{};if(!Rel32To(from+5,address+prologue,rel))return false;
        e.Bytes({0xe9});e.U32(rel);
        const u64 original=PlaceStub(e);if(!original)return false;
        if(site.offset==HeapFree)original_heap_free=reinterpret_cast<HeapFreeFn>(original);
        else if(site.offset==HeapPlainAlloc)original_heap_plain=reinterpret_cast<HeapPlainFn>(original);
        else original_heap_aligned=reinterpret_cast<HeapAlignedFn>(original);
        e=Emitter{};e.Bytes({0xff,0x25,0,0,0,0});e.U64(reinterpret_cast<u64>(wrapper));
    } else {
        e.SaveFull();e.Bytes({0x4c,0x89,0xf7}); // mov rdi,r14 (node)
        e.CallAbs(reinterpret_cast<const void*>(&RangeFreeHook));e.RestoreFull();
        e.Bytes({0x48,0x8b,0x4c,0x24,0x28});
        const u64 from=reinterpret_cast<u64>(stub_page+stub_used)+e.size;
        std::uint32_t rel{};if(!Rel32To(from+5,address+5,rel))return false;
        e.Bytes({0xe9});e.U32(rel);
    }
    const u64 stub=PlaceStub(e);std::uint32_t rel{};
    if(!stub || !Rel32To(address+5,stub,rel))return false;
    site.stub.store(stub,std::memory_order_release);
    const bool valid=site.offset==HeapFree ? Patch(address,HeapFreeCode,sizeof(HeapFreeCode)) :
        site.offset==HeapPlainAlloc ? Patch(address,HeapPlainCode,sizeof(HeapPlainCode)) :
        site.offset==HeapAlignedAlloc ? Patch(address,HeapAlignedCode,sizeof(HeapAlignedCode)) :
        Patch(address,RangeFreeCode,sizeof(RangeFreeCode));
    if(!valid)return false;
    WriteJumpOverTrap(address,0xe9,rel);return true;
}
} // namespace

bool FexHooksPrepared() {return fex_detours_complete.load(std::memory_order_acquire);}
void ConfigureFexHooks(bool complete) {
    std::scoped_lock lock{fex_configuration_mutex};
    fex_detours_complete.store(complete,std::memory_order_release);
    // Configuration alone cannot authorize reclamation: the remote observer
    // and fresh-range callback must be installed too.
    BbResourceLifetime::SetReclaimReady(complete && fex_callback_ready.load(std::memory_order_acquire));
}
void FexFreshRange(u64 address,u64 size) {if(auto callback=fex_on_range.load(std::memory_order_acquire))callback(address,size);}
bool FexGpuScope(u64 address) {return GpuScope(address);}
void Install(RangeCallback on_gpu_range_allocated) {
#if defined(__aarch64__)
    if(BbRemote::BackActive())BbRemote::Back::InstallGuestHooks(on_gpu_range_allocated);
    else {
        std::scoped_lock lock{fex_configuration_mutex};
        if(fex_detours_complete.load(std::memory_order_acquire)) {
            fex_on_range.store(on_gpu_range_allocated,std::memory_order_release);
            fex_callback_ready.store(on_gpu_range_allocated!=nullptr,std::memory_order_release);
            BbResourceLifetime::SetReclaimReady(on_gpu_range_allocated!=nullptr);
            std::printf("Guest hooks: FEX startup detours ready, remote lifetime callback=%d\n",on_gpu_range_allocated!=nullptr);
            return;
        }
        // Never install the x86 native/SIGTRAP path in the ARM frontend.
        BbResourceLifetime::SetReclaimReady(false);
        static std::atomic<bool> told{false};
        if(!told.exchange(true))std::fputs("Guest hooks: native frontend requires FEX detours; reclaim leases stay disabled\n",stderr);
    }
    return; // CPU role is explicit; architecture alone no longer identifies it.
#endif
    static std::atomic<bool> installed{false};
    if (installed.exchange(true)) {
        return;
    }
    struct sigaction action {};
    action.sa_sigaction = OnTrap;
    action.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTRAP, &action, &previous_action);
    int patched = 0;
    for (const auto& site : MemcpySites) {
        patched += Patch(ImageBase + site.offset, site.code, sizeof(site.code));
    }
    std::printf("Guest hooks: %d of %zu resource copy sites tell the GPU side of their writes\n",
                patched, MemcpySites.size());
    on_range_allocated = on_gpu_range_allocated;
    const bool allocator = Patch(ImageBase + AllocReturn, AllocReturnCode, sizeof(AllocReturnCode));
    // BB_HEAP_HOOKS=0 (bisecting): the heap's ranges are not reported.
    const char* heap_env = std::getenv("BB_HEAP_HOOKS");
    const bool heap_hooks = !heap_env || heap_env[0] != '0';
    if (BbStats::enabled) {
        Patch(ImageBase + CollectorEntry, CollectorEntryCode, sizeof(CollectorEntryCode));
    }
    int heap_sites = 0;
    for (const u64 site : HeapAllocReturns) {
        if (!heap_hooks) {
            break;
        }
        heap_sites += Patch(ImageBase + site, HeapAllocReturnCode, sizeof(HeapAllocReturnCode));
    }
    std::printf("Guest hooks: GPU heap allocations seen at %d of %zu call sites\n", heap_sites,
                HeapAllocReturns.size());
    std::printf("Guest hooks: GPU memory allocator %s\n", allocator ? "tells the GPU side of new ranges" : "not found");
    // Native detours over the traps just placed (BB_GUEST_HOOK_TRAPS=1 keeps the traps).
    const char* traps_env = std::getenv("BB_GUEST_HOOK_TRAPS");
    if (!(traps_env && traps_env[0] == '1') && (stub_page = MapStubPage()) != nullptr) {
        int detours = 0;
        for (const auto& site : MemcpySites) {
            if (reinterpret_cast<const u8*>(ImageBase + site.offset)[0] == 0xcc) {
                detours += InstallCallDetour(site.offset);
            }
        }
        for (auto& site : std::span(native_sites).first(4)) {
            if (reinterpret_cast<const u8*>(ImageBase + site.offset)[0] == 0xcc) {
                detours += InstallDetour(site);
            }
        }
        if (BbResourceLifetime::Enabled()) {
            const auto active=[](const char* key) {const char* e=std::getenv(key);return e && e[0]=='1';};
            // These optional diagnostics patch the collector too; never stack native hooks.
            const bool conflict=active("BB_FREE_CHECK") || active("BB_LABEL_TRAP");
            const bool range_free=!conflict && allocator && InstallLifetimeFree(native_sites[4]);
            const bool plain=InstallLifetimeFree(native_sites[6]);
            const bool aligned=InstallLifetimeFree(native_sites[7]);
            heap_entry_hooks=plain && aligned;
            const bool heap_free=InstallLifetimeFree(native_sites[5]);
            const bool alloc_native=std::all_of(native_sites.begin(),native_sites.begin()+4,
                [](const auto& s){return s.stub.load(std::memory_order_acquire)!=0;});
            const bool complete=allocator && heap_sites==3 && range_free && heap_free && alloc_native && heap_entry_hooks;
            BbResourceLifetime::SetReclaimReady(complete);
            std::printf("Guest resource lifetime hooks: range-alloc=%d/1 heap-alloc=%d/3 range-free=%d/1 heap-free=%d/1 heap-entry-alloc=%d/2 diagnostic-conflict=%d reclaim-ready=%d gpu-protection-filter=on\n",allocator,heap_sites,range_free,heap_free,int(plain)+int(aligned),conflict,complete);
        }
        mprotect(stub_page, StubPageSize, PROT_READ | PROT_EXEC);
        std::printf("Guest hooks: %d native detours (no traps; BB_GUEST_HOOK_TRAPS=1: int3)\n",
                    detours);
    }
    BbHeapSites::Install();
    if (const char* env = std::getenv("BB_HEAP_SITES"); env && env[0] == '1') {
        Patch(ImageBase + ReleaseCheck, ReleaseCheckCode, sizeof(ReleaseCheckCode));
    }
}
} // namespace BbGuestHooks
