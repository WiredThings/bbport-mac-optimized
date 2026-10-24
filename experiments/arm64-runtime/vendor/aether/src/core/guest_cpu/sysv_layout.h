// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <algorithm>

namespace Core::GuestCpu {
// C++20 has no field reflection. Unknown aggregates fail closed; declarations must
// describe actual member types/offsets rather than guessing from sizeof(T).
template<class T> struct GuestAggregate { static constexpr bool enabled = false; };
template<class T> struct GuestVector128 : std::false_type {};
template<class T, std::size_t Offset> struct GuestField { using type = T; static constexpr auto offset = Offset; };
enum class Eightbyte { None, Integer, SSE, SSEUp };
struct SysVLayout {
 bool supported{}, memory{};
 std::size_t size{}, alignment{};
 std::array<Eightbyte,2> slots{};
};
namespace sysv {
constexpr Eightbyte Merge(Eightbyte a, Eightbyte b) {
 if(a==Eightbyte::None)return b;if(b==Eightbyte::None||a==b)return a;
 if(a==Eightbyte::Integer||b==Eightbyte::Integer)return Eightbyte::Integer;
 return Eightbyte::SSE;
}
template<class T> constexpr bool Registered = std::is_integral_v<T> || std::is_enum_v<T> || std::is_pointer_v<T> || std::is_same_v<T,float> || std::is_same_v<T,double> || GuestVector128<T>::value || GuestAggregate<T>::enabled;
template<class T> constexpr void Place(SysVLayout& out,std::size_t offset) {
 if constexpr(!Registered<T> || !std::is_trivially_copyable_v<T>) {out.supported=false;}
 else if(offset>out.size || sizeof(T)>out.size-offset){out.supported=false;}
 else if(offset%alignof(T)){out.memory=true;}
 else if constexpr(GuestAggregate<T>::enabled) {
  std::apply([&](auto...f){(Place<typename decltype(f)::type>(out,offset+decltype(f)::offset),...);},GuestAggregate<T>::fields);
 } else if constexpr(GuestVector128<T>::value) {
  if constexpr(sizeof(T)!=16){out.supported=false;}
  else if(offset%16){out.memory=true;}
  else if(offset+16<=16){out.slots[offset/8]=Merge(out.slots[offset/8],Eightbyte::SSE);out.slots[offset/8+1]=Merge(out.slots[offset/8+1],Eightbyte::SSEUp);}
 } else {
  if constexpr(sizeof(T)>8){out.supported=false;}
  else {if(out.memory)return;auto kind=(std::is_same_v<T,float>||std::is_same_v<T,double>)?Eightbyte::SSE:Eightbyte::Integer;
   for(std::size_t i=offset/8;i<=(offset+sizeof(T)-1)/8&&i<2;++i)out.slots[i]=Merge(out.slots[i],kind);}
 }
}
template<class T> constexpr SysVLayout Layout() {
 SysVLayout out{};
 if constexpr(std::is_void_v<T>){out.supported=true;return out;}
 else if constexpr(!Registered<T>||!std::is_trivially_copyable_v<T>||std::is_reference_v<T>){return out;}
 else {out.supported=true;out.size=sizeof(T);out.alignment=std::max<std::size_t>(8,alignof(T));out.memory=sizeof(T)>16;
  if(out.alignment>16||sizeof(T)>256){out.supported=false;return out;}
  Place<T>(out,0);
  if(out.slots[1]==Eightbyte::SSEUp && out.slots[0]!=Eightbyte::SSE)out.slots[1]=Eightbyte::SSE;
  return out;
 }
}
}
}
