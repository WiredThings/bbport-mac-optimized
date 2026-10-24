// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <type_traits>
#include <limits>
#include <new>
extern "C" uint64_t bbnet_guest_call(uintptr_t,const uint64_t*,size_t);
extern "C" void* bbnet_guest_alloc(uint64_t,uint64_t);
extern "C" void bbnet_guest_free(void*);
namespace BbNet {
template<class T> uint64_t Word(T v) {
 if constexpr(std::is_pointer_v<T>)return reinterpret_cast<uintptr_t>(v);
 else {static_assert(std::is_integral_v<T>||std::is_enum_v<T>);return static_cast<uint64_t>(v);}
}
template<class...A> uint64_t CallWords(uintptr_t fn,A...args) {
 static_assert(sizeof...(A)<=7);std::array<uint64_t,sizeof...(A)> words{Word(args)...};
 return fn?bbnet_guest_call(fn,words.data(),words.size()):0;
}
template<class F,class...A> auto CallGuest(F fn,A...args)->std::invoke_result_t<F,A...> {
 static_assert(std::is_pointer_v<F> && std::is_function_v<std::remove_pointer_t<F>>, "Only guest function pointers enter FEX; host closures execute natively");
 using R=std::invoke_result_t<F,A...>;auto value=CallWords(reinterpret_cast<uintptr_t>(fn),args...);
 if constexpr(std::is_void_v<R>)return;
 else if constexpr(std::is_pointer_v<R>)return reinterpret_cast<R>(value);
 else return static_cast<R>(value);
}
template<class T> struct LowAllocator {
 using value_type=T;LowAllocator()=default;template<class U>LowAllocator(const LowAllocator<U>&){}
 T*allocate(size_t n){if(n>std::numeric_limits<size_t>::max()/sizeof(T))throw std::bad_alloc();auto*p=bbnet_guest_alloc(n*sizeof(T),alignof(T)>16?alignof(T):16);if(!p)throw std::bad_alloc();return static_cast<T*>(p);}
 void deallocate(T*p,size_t){bbnet_guest_free(p);}
 template<class U>bool operator==(const LowAllocator<U>&)const{return true;}
};
}
