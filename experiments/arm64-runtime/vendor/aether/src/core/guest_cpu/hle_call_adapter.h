#include "bb_guest_frame_policy.h"
#include "bb_guest_sync_policy.h"
// SPDX-License-Identifier: MIT
#pragma once

#include "common/types.h"
#include "guest_cpu.h"
#include "sysv_layout.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <unordered_map>
#include <vector>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
#include "core/ios/ios_jit_allocator.h"
#endif
#if defined(__APPLE__) && !TARGET_OS_IPHONE
#include "bb_darwin_veneer_slab.h"
#endif

namespace Core::GuestCpu {

// This is the x86-64 SysV state at the FEX syscall boundary. Guest pointers
// remain guest virtual addresses; their range is checked before native HLE sees
// them. The caller owns the validator and any mapping lifetime it depends on.
struct HleCallFrame final {
    u64 operation{};
    std::array<u64, 16> gpr{};
    std::array<std::array<u64, 2>, 16> xmm{};
    std::uintptr_t rsp{};
    bool (*validate_range)(void* context, std::uintptr_t address, std::size_t size,
                           bool writable){};
    void* validate_context{};
    bool (*publish_host_range)(void* context, std::uintptr_t address, std::size_t size,
                               bool writable){};
    bool (*revoke_host_range)(void* context, std::uintptr_t address){};
    void* host_range_context{};
};

[[nodiscard]] bool PublishHostRange(const void* pointer, std::size_t size, bool writable);
[[nodiscard]] bool RevokeHostRange(const void* pointer);

struct HleCallFailure final {
    int error{};
    std::string_view name;
};

using HleCallResult = std::variant<bool, HleCallFailure>;

class HleCallAdapter {
public:
    virtual ~HleCallAdapter() = default;

    [[nodiscard]] u64 Operation() const noexcept { return operation; }
    [[nodiscard]] std::string_view Name() const noexcept { return name; }
    virtual HleCallResult Invoke(HleCallFrame& frame) const = 0;

    // Set (via exchange, so exactly one caller sees false) by HleGuestBridge::Invoke the first
    // time this specific function is ever called in the process, regardless of how many total
    // HLE calls have happened -- see its own comment for why a global call-count trace budget
    // (the previous scheme) misses this.
    mutable std::atomic<bool> traced_once{false};
    bool stutter_wait{false};
    uint8_t guest_frame_stage{};
    BbGuestSync::Stage guest_sync_stage{};

private:
    friend class HleCallRegistry;

    void AssignOperation(u64 operation_, std::string_view name_) {
        operation = operation_;
        name = name_;
        guest_frame_stage=BbGuestFrame::Classify(name_);
        guest_sync_stage=BbGuestSync::Classify(name_);
        for(auto token : {"wait","Wait","mutex","cond_","sema","rwlock","sleep","Sleep","_read","_pread","ReadData","ReadFile","write","Write","join","Join"})
            if(name_.find(token)!=std::string_view::npos)stutter_wait=true;
    }

    u64 operation{};
    std::string name;
};

class HleCallRegistry final {
public:
    ~HleCallRegistry();
    // Borrowed only while the registry lives. Entries are append-only and retain
    // their adapters until registry destruction; guest workers must be joined first.
    [[nodiscard]] HleCallAdapter* FindBorrowed(u64 operation) const;
    std::shared_ptr<HleCallAdapter> Register(std::shared_ptr<HleCallAdapter> adapter,
                                             std::string_view name);
    [[nodiscard]] std::shared_ptr<HleCallAdapter> Find(u64 operation) const;

private:
    static constexpr size_t FastPageEntries=128, FastPageCount=64;
    struct PublishedPage { std::array<std::atomic<HleCallAdapter*>,FastPageEntries> entries{}; };
    static_assert(std::atomic<HleCallAdapter*>::is_always_lock_free);
    // Fixed 1 KiB-or-less root; lazy 1 KiB pages contain pointers, not extra owners.
    // Larger operation IDs still use the unbounded original vector/lock path.
    std::array<std::atomic<PublishedPage*>,FastPageCount> published_pages{};
    const bool fast_lookup=[] {const auto* value=std::getenv("BB_ARM_HLE_FAST");return value&&value[0]=='1';}();
    mutable std::shared_mutex registry_mutex;
    u64 next_operation{1};
    std::vector<std::shared_ptr<HleCallAdapter>> adapters;
};

struct HleVeneerFailure final {
    int error{};
};

using HleVeneerResult = std::variant<u64, HleVeneerFailure>;

class HleVeneerAllocator final {
public:
    HleVeneerAllocator() = default;
    HleVeneerAllocator(const HleVeneerAllocator&) = delete;
    HleVeneerAllocator& operator=(const HleVeneerAllocator&) = delete;
    ~HleVeneerAllocator();

    HleVeneerResult Allocate(const HleCallAdapter& adapter);
    [[nodiscard]] const char* LayoutName() const noexcept;

    [[nodiscard]] std::vector<GuestExecutionRange> GetExecutableRanges() const;
    // Dynamic modules allocate veneers after FEX thread start. FEX JIT must
    // discover those pages through QueryGuestExecutableRange, not only the
    // MappedRanges snapshot taken at Run().
    [[nodiscard]] std::optional<GuestExecutionRange> QueryExecutableRange(
        std::uintptr_t address) const;

private:
    struct Allocation final {
        void* page{};
        std::size_t size{};
    };

#if defined(__APPLE__) && TARGET_OS_IPHONE
    // Every veneer is only 16 bytes, but allocating one mmap+mprotect(PROT_EXEC) page per
    // veneer individually (the non-iOS path below still does exactly that) was confirmed
    // on-device as severe enough to hang app launch entirely for a game referencing a few
    // hundred distinct system functions -- likely each mprotect(PROT_EXEC) transition
    // paying real per-call codesign/AMFI verification overhead on iOS, unlike the
    // effectively-free page-table bit flip it is on Linux. Batches many veneers into one
    // shared dual-mapped region instead (same mechanism as everywhere else in this port
    // needs for runtime code generation -- see ios_jit_allocator.h): rw_addr is where new
    // veneers get written, rx_addr is what gets handed out as the callable address, and
    // neither alias's protection ever changes after the batch is created. That matters for
    // correctness, not just speed -- toggling a single mmap+mprotect page back to writable
    // to add more veneers to it would race any guest thread already executing an earlier
    // veneer in that same page; two permanently-separate aliases have no such race.
    struct VeneerBatch final {
        Core::DualMappedRegion region;
        std::size_t used{};
    };
    std::vector<VeneerBatch> batches;
#endif

#if defined(__APPLE__) && !TARGET_OS_IPHONE
    struct GuestSlab final {std::unique_ptr<BbDarwinVeneer::Slab> region;std::size_t used{};};
    std::vector<GuestSlab> guest_slabs;
    const bool packed_veneers=[] {const char* value=std::getenv("BB_ARM_PACKED_VENEERS");return value&&value[0]=='1';}();
#endif

    mutable std::mutex allocator_mutex;
    std::vector<Allocation> allocations;
    std::vector<GuestExecutionRange> executable_ranges;
    std::unordered_map<u64, u64> veneers;
};

namespace detail {

constexpr std::array<std::size_t, 6> SysVIntegerArgs{7, 6, 2, 1, 8, 9};

struct CallCursor final {
 HleCallFrame& frame;
 std::size_t integer_index{}, vector_index{}, stack_offset{sizeof(u64)};
 template<class T> std::optional<T> Read() {
  constexpr auto layout=sysv::Layout<T>();
  if constexpr(!layout.supported){return std::nullopt;}
  else {
   std::size_t integers=0,vectors=0;
   for(auto slot:layout.slots){integers+=slot==Eightbyte::Integer;vectors+=slot==Eightbyte::SSE;}
   T value{};auto* bytes=reinterpret_cast<unsigned char*>(&value);
   // A whole aggregate rolls back to memory if either register class is exhausted.
   if(layout.memory || integer_index+integers>6 || vector_index+vectors>8){
    const auto alignment=layout.alignment;
    if(frame.rsp>UINTPTR_MAX-stack_offset)return std::nullopt;
    const auto unaligned=frame.rsp+stack_offset;
    if(unaligned>UINTPTR_MAX-(alignment-1))return std::nullopt;
    const auto address=(unaligned+alignment-1)&~(alignment-1);
    if(address>UINTPTR_MAX-sizeof(T))return std::nullopt;
    if(!frame.validate_range || !frame.validate_range(frame.validate_context,address,sizeof(T),false))return std::nullopt;
    std::memcpy(bytes,reinterpret_cast<const void*>(address),sizeof(T));
    stack_offset=address-frame.rsp+((sizeof(T)+7)&~std::size_t{7});
   }else{
    std::size_t previous_vector=0;
    for(std::size_t i=0;i<(sizeof(T)+7)/8;++i){
     u64 word{};
     switch(layout.slots[i]){
      case Eightbyte::Integer:word=frame.gpr[SysVIntegerArgs[integer_index++]];break;
      case Eightbyte::SSE:previous_vector=vector_index++;word=frame.xmm[previous_vector][0];break;
      case Eightbyte::SSEUp:word=frame.xmm[previous_vector][1];break;
      case Eightbyte::None:break;
     }
     std::memcpy(bytes+i*8,&word,std::min<std::size_t>(8,sizeof(T)-i*8));
    }
   }
   return value;
  }
 }
};
template <typename T> inline constexpr bool IsSupportedArgument = sysv::Layout<T>().supported;
template <typename T> inline constexpr bool IsSupportedReturn = sysv::Layout<T>().supported && !sysv::Layout<T>().memory;
template <typename T> std::optional<T> DecodeArgument(CallCursor& cursor) {
 auto value=cursor.Read<T>();
 if constexpr(std::is_pointer_v<T> && !std::is_function_v<std::remove_pointer_t<T>>){
  if(value && *value){
   using Pointee=std::remove_cv_t<std::remove_pointer_t<T>>;
   constexpr auto minimum=[] {if constexpr(std::is_void_v<Pointee>||!requires{sizeof(Pointee);})return std::size_t{1};else return sizeof(Pointee);}();
   if(!cursor.frame.validate_range || !cursor.frame.validate_range(cursor.frame.validate_context,reinterpret_cast<std::uintptr_t>(*value),minimum,!std::is_const_v<std::remove_pointer_t<T>>))return std::nullopt;
  }
 }
 return value;
}
template <typename Return> void EncodeReturn(HleCallFrame& frame, Return&& value) {
 using T=std::remove_cvref_t<Return>;
 if constexpr(std::is_pointer_v<T>)frame.gpr[0]=reinterpret_cast<std::uintptr_t>(value);
 else if constexpr(std::is_integral_v<T>||std::is_enum_v<T>)frame.gpr[0]=static_cast<u64>(value);
 else {
  constexpr auto layout=sysv::Layout<T>();std::size_t integers=0,vectors=0,previous_vector=0;
  for(std::size_t i=0;i<(sizeof(T)+7)/8;++i){u64 word{};std::memcpy(&word,reinterpret_cast<const unsigned char*>(&value)+i*8,std::min<std::size_t>(8,sizeof(T)-i*8));
   switch(layout.slots[i]){
    case Eightbyte::Integer:frame.gpr[integers++==0?0:2]=word;break;
    case Eightbyte::SSE:previous_vector=vectors++;frame.xmm[previous_vector]={word,0};break;
    case Eightbyte::SSEUp:frame.xmm[previous_vector][1]=word;break;
    case Eightbyte::None:break;
   }
  }
 }
}

template <typename Function>
class TypedHleCallAdapter;

template <typename Return, typename... Args>
class TypedHleCallAdapter<Return (*)(Args...)> final : public HleCallAdapter {
public:
    explicit TypedHleCallAdapter(Return (*function_)(Args...)) : function{function_} {}

    HleCallResult Invoke(HleCallFrame& frame) const override {
        if constexpr ((!IsSupportedArgument<Args> || ...) || !IsSupportedReturn<Return>) {
            return HleCallFailure{ENOTSUP, Name()};
        } else {
            CallCursor cursor{frame};
            std::tuple<std::optional<Args>...> decoded{DecodeArgument<Args>(cursor)...};
            if (!AllDecoded(decoded, std::index_sequence_for<Args...>{})) {
                return HleCallFailure{EFAULT, Name()};
            }
            if constexpr (std::is_void_v<Return>) {
                InvokeVoid(decoded, std::index_sequence_for<Args...>{});
                frame.gpr[0] = 0;
            } else {
                const auto result = InvokeResult(decoded, std::index_sequence_for<Args...>{});
                EncodeReturn(frame, result);
            }
            return true;
        }
    }

private:
    template <std::size_t... Index>
    static bool AllDecoded(const std::tuple<std::optional<Args>...>& decoded,
                           std::index_sequence<Index...>) {
        return (std::get<Index>(decoded).has_value() && ...);
    }

    template <std::size_t... Index>
    void InvokeVoid(const std::tuple<std::optional<Args>...>& decoded,
                    std::index_sequence<Index...>) const {
        function(*std::get<Index>(decoded)...);
    }

    template <std::size_t... Index>
    Return InvokeResult(const std::tuple<std::optional<Args>...>& decoded,
                        std::index_sequence<Index...>) const {
        return function(*std::get<Index>(decoded)...);
    }

    Return (*function)(Args...);
};

class UnsupportedHleCallAdapter final : public HleCallAdapter {
public:
    HleCallResult Invoke(HleCallFrame&) const override {
        return HleCallFailure{ENOSYS, Name()};
    }
};

} // namespace detail

template <typename Function>
std::shared_ptr<HleCallAdapter> MakeHleCallAdapter(Function function) {
    using Signature = decltype(function);
    return std::make_shared<detail::TypedHleCallAdapter<Signature>>(function);
}

inline std::shared_ptr<HleCallAdapter> MakeUnsupportedHleCallAdapter() {
    return std::make_shared<detail::UnsupportedHleCallAdapter>();
}

} // namespace Core::GuestCpu
