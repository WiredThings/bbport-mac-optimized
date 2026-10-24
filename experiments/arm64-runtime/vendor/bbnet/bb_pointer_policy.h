// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <algorithm>
// Compile-time rules refer to the C++ name in the *actual* LIB_FUNCTION macro.
// No runtime name lookup, guest buffer copies, or retained allocation is added.
namespace BbNet {
struct DataRange {
 unsigned pointer{}, length{}, unit{1}; bool writable{}, indirect{};
 constexpr bool operator==(const DataRange&)const=default;
};
struct DataPolicy {
 uint64_t strings{};
 std::array<unsigned,8> read{},write{};
 std::array<DataRange,3> ranges{};
 unsigned count{};
 constexpr bool operator==(const DataPolicy&)const=default;
 constexpr void Span(unsigned p,unsigned n,bool w,unsigned unit=1,bool indirect=false){ranges[count++]={p,n,unit,w,indirect};}
};
constexpr DataPolicy DataRule(std::string_view n){
 DataPolicy p;
 // Only consumed C strings: ignored labels/options retain the original ABI checks.
 if(n=="net_inet_pton")p.strings=2;
 if(n=="webapi_create_req")p.strings=4;
 if(n=="webapi_hdr_len"||n=="webapi_hdr_val")p.strings=2;
 if(n=="webapi_parse_npid")p.strings=1;
 if(n=="sceHttpCreateConnectionWithURL"||n=="sceHttpCreateTemplate")p.strings=2;
 if(n=="sceHttpCreateRequestWithURL")p.strings=4;
 if(n=="sceHttpAddRequestHeader")p.strings=6;
 if(n=="sceHttpRemoveRequestHeader"||n=="sceHttpGetResponseHeader")p.strings=2;
 if(n=="webapi_hdr_val")p.Span(2,3,true);
 if(n=="net_bind")p.Span(1,2,false);
 if(n=="net_setsockopt")p.Span(3,4,false);
 if(n=="net_sendto")p.Span(4,5,false);
 if(n=="net_accept"||n=="net_getsockname"||n=="net_getsockopt")p.Span(n=="net_getsockopt"?3:1,n=="net_getsockopt"?4:2,true,1,true);
 if(n=="net_recvfrom")p.Span(4,5,true,1,true);
 if(n=="net_inet_ntop"){p.read[1]=4;p.Span(2,3,true);}
 if(n=="net_inet_pton")p.write[2]=4;
 if(n=="net_epoll_control")p.read[3]=24;
 if(n=="net_epoll_wait")p.Span(1,2,true,24);
 if(n=="net_resolver_ntoa")p.write[2]=4;
 if(n=="net_resolver_aton")p.Span(2,3,true);
 if(n=="ctl_get_info")p.write[1]=16;
 if(n=="ctl_nat")p.write[0]=12;
 if(n=="sig_activate")p.read[1]=16;
 if(n=="np_online_id")p.write[1]=20;
 if(n=="np_get_npid"||n=="webapi_parse_npid")p.write[1]=36;
 if(n=="np_cmp_online")p.read[0]=p.read[1]=16;
 if(n=="np_cmp_npid")p.read[0]=p.read[1]=36;
 if(n=="np_parental")p.write[3]=3;
 if(n=="npauth_get_code")p.write[2]=128;
 if(n=="lookup_npid"){p.read[1]=16;p.write[2]=36;}
 if(n=="m2_create_join_room")p.read[1]=2;
 if(n=="m2_join_room")p.read[1]=8;
 return p;
}
template<class T>uint64_t PolicyWord(T value){
 if constexpr(std::is_pointer_v<T>)return reinterpret_cast<uintptr_t>(value);
 else if constexpr(std::is_integral_v<T>||std::is_enum_v<T>)return static_cast<uint64_t>(value);
 else return 0;
}
template<class Frame>bool DataSpan(Frame&f,uintptr_t a,uint64_t n,bool writable){
 return n==0 || (a && n<=SIZE_MAX && a<=UINTPTR_MAX-n && f.validate_range && f.validate_range(f.validate_context,a,static_cast<size_t>(n),writable));
}
template<class Frame>bool DataString(Frame&f,uintptr_t a){
 if(!a)return true; // API owns its null-argument error behavior.
 // Scan in bounded stack-free chunks, with a byte tail when a range ends.
 // A single string over 1 MiB is rejected, not truncated or copied.
 for(size_t used=0;used<1024*1024;){
  const size_t n=std::min<size_t>(64,1024*1024-used);
  if(a>UINTPTR_MAX-used)return false;
  const uintptr_t at=a+used;
  if(DataSpan(f,at,n,false)){
   if(std::memchr(reinterpret_cast<const void*>(at),0,n))return true;
   used+=n;
  }else{
   if(!DataSpan(f,at,1,false))return false;
   if(!*reinterpret_cast<const unsigned char*>(at))return true;
   ++used;
  }
 }
 return false;
}
template<class Frame,size_t N>bool ValidateData(Frame&f,const std::array<uint64_t,N>&v,DataPolicy p){
 for(size_t i=0;i<N&&i<8;++i){
  if((p.strings&(uint64_t{1}<<i))&&!DataString(f,v[i]))return false;
  // Optional null arguments remain for the original API to validate.
  if(v[i]&&p.read[i]&&!DataSpan(f,v[i],p.read[i],false))return false;
  if(v[i]&&p.write[i]&&!DataSpan(f,v[i],p.write[i],true))return false;
 }
 for(unsigned i=0;i<p.count;++i){
  auto r=p.ranges[i];if(r.pointer>=N||r.length>=N)return false;
  uint64_t n=v[r.length];
  if(r.indirect){
   if(!n||!v[r.pointer])continue;
   if(!DataSpan(f,n,sizeof(uint32_t),true))return false;
   uint32_t count;std::memcpy(&count,reinterpret_cast<const void*>(n),sizeof(count));n=count;
  }
  if(n>UINT64_MAX/r.unit||!DataSpan(f,v[r.pointer],n*r.unit,r.writable))return false;
 }
 return true;
}
} // namespace BbNet
