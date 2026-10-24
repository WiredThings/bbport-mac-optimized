// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/guest_cpu/hle_call_adapter.h"
#include "bb_pointer_policy.h"
namespace BbNet {
// Only API-declared code addresses and opaque user tokens bypass data-pointer
// decoding. Buffers, structures and output slots retain the exact range checks.
constexpr uint64_t OpaqueMask(std::string_view name) {
 if(name=="webapi_reg_push")return (uint64_t{1}<<2)|(uint64_t{1}<<3);
 // WebAPI stores these void* values and later passes them back unchanged.
 // They are not readable/writable buffers; function-pointer decoding is separate.
 if(name=="sceNpWebApiRegisterNotificationCallback")return uint64_t{1}<<2;
 if(name=="sceNpWebApiRegisterPushEventCallback"||name=="sceNpWebApiRegisterServicePushEventCallback"||name=="sceNpWebApiRegisterExtdPushEventCallback"||name=="sceNpWebApiRegisterExtdPushEventCallbackA"||name=="sceNpWebApiIntRegisterServicePushEventCallback"||name=="sceNpWebApiIntRegisterServicePushEventCallbackA"||name=="sceNpWebApi2PushEventRegisterCallback"||name=="sceNpWebApi2PushEventRegisterPushContextCallback")return uint64_t{1}<<3;
 if(name=="sceHttpWaitRequest"||name=="sceHttpAbortWaitRequest")return 1;
 if(name=="sceHttpDestroyEpoll")return 2;
 if(name=="sceHttpSetEpoll")return 6;
 if(name=="m2_register_context_cb"||name=="np_register_cb"||name=="ctl_register")return 3;
 if(name=="m2_register_room_event_cb"||name=="m2_register_signaling_cb"||name=="m2_register_lobby_event_cb"||name=="sig_create_ctx")return 6;
 if(name=="sceNetCtlRegisterCallback"||name=="sceNetCtlRegisterCallbackForNpToolkit"||name=="sceNpRegisterStateCallback"||name=="sceNpRegisterStateCallbackA"||name=="sceNpRegisterStateCallbackForNpToolkit"||name=="sceNpRegisterNpReachabilityStateCallback")return 2;
 if(name=="sceHttpSetAuthInfoCallback"||name=="sceHttpSetCookieRecvCallback"||name=="sceHttpSetCookieSendCallback"||name=="sceHttpSetRedirectCallback"||name=="sceHttpSetRequestStatusCallback"||name=="sceHttpsSetSslCallback")return 4;
 return 0;
}
// Explicit byte spans from the API declaration; no guessed count/size heuristics.
enum class ByteSpan : uint64_t { None, InputArg1Length2, OutputArg1Length2 };
constexpr ByteSpan ByteSpanRule(std::string_view name) {
 if(name=="sceHttpSendRequest"||name=="sceNetSend"||name=="net_send"||name=="net_sendto"||name=="webapi_send")return ByteSpan::InputArg1Length2;
 if(name=="sceHttpReadData"||name=="sceNetRecv"||name=="net_recv"||name=="net_recvfrom"||name=="webapi_read")return ByteSpan::OutputArg1Length2;
 return ByteSpan::None;
}
template<class F,ByteSpan Span=ByteSpan::None,DataPolicy Policy={}>class TypedAdapter;
template<ByteSpan Span,DataPolicy Policy,class R,class...A>class TypedAdapter<R(*)(A...),Span,Policy> final:public Core::GuestCpu::HleCallAdapter {
 using Function=R(*)(A...);Function fn;uint64_t mask;
 template<size_t I,class T>auto Decode(Core::GuestCpu::detail::CallCursor&cursor)const {
  if constexpr(std::is_pointer_v<T>)if(mask&(uint64_t(1)<<I)){
   auto word=cursor.Read<uint64_t>();return word?std::optional<T>(reinterpret_cast<T>(*word)):std::nullopt;
  }
  // Some legacy declarations use mutable void* for read-only input structs.
  // The explicit full read rule, rather than that spelling, owns access rights.
  if constexpr(I<8 && std::is_pointer_v<T> && !std::is_function_v<std::remove_pointer_t<T>>){
   if constexpr(Policy.read[I] || (Policy.strings&(uint64_t{1}<<I))){
    auto value=cursor.Read<T>();
    if(value && *value && !DataSpan(cursor.frame,reinterpret_cast<uintptr_t>(*value),1,false))return std::optional<T>{};
    return value;
   }
  }
  return Core::GuestCpu::detail::DecodeArgument<T>(cursor);
 }
 template<size_t...I>Core::GuestCpu::HleCallResult Call(Core::GuestCpu::HleCallFrame&frame,std::index_sequence<I...>)const {
  using namespace Core::GuestCpu;
  detail::CallCursor cursor{frame};std::tuple<std::optional<A>...> values{Decode<I,A>(cursor)...};
  if(!(std::get<I>(values).has_value()&&...))return HleCallFailure{EFAULT,Name()};
  if constexpr(Span!=ByteSpan::None) {
   using Args=std::tuple<A...>;
   static_assert(sizeof...(A)>2 && std::is_pointer_v<std::tuple_element_t<1,Args>> && std::is_unsigned_v<std::tuple_element_t<2,Args>>);
   constexpr bool writable=Span==ByteSpan::OutputArg1Length2;
   static_assert(!writable || !std::is_const_v<std::remove_pointer_t<std::tuple_element_t<1,Args>>>);
   const auto address=reinterpret_cast<uintptr_t>(*std::get<1>(values));
   const auto length=*std::get<2>(values);
#ifndef BB_NET_OLD_MINIMUM_RANGE
   if(length && (!address || length>SIZE_MAX || address>UINTPTR_MAX-length || !frame.validate_range || !frame.validate_range(frame.validate_context,address,static_cast<size_t>(length),writable)))return HleCallFailure{EFAULT,Name()};
#endif
  }
  if constexpr(Policy!=DataPolicy{}) {
   const std::array<uint64_t,sizeof...(A)> words{PolicyWord(*std::get<I>(values))...};
   if(!ValidateData(frame,words,Policy))return HleCallFailure{EFAULT,Name()};
  }
  if constexpr(std::is_void_v<R>){fn(*std::get<I>(values)...);frame.gpr[0]=0;}
  else detail::EncodeReturn(frame,fn(*std::get<I>(values)...));
  return true;
 }
public:TypedAdapter(Function f,uint64_t m):fn(f),mask(m){}
 Core::GuestCpu::HleCallResult Invoke(Core::GuestCpu::HleCallFrame&frame)const override {
  if constexpr((!Core::GuestCpu::detail::IsSupportedArgument<A>||...)||!Core::GuestCpu::detail::IsSupportedReturn<R>)return Core::GuestCpu::HleCallFailure{ENOTSUP,Name()};
  else return Call(frame,std::index_sequence_for<A...>{});
 }
};
}
