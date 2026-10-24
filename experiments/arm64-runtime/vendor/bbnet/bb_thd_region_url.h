// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <span>
#include <string_view>
#include <array>
#include <cstdint>
#include <cstring>
namespace BbThdRegionUrl {
// Verified 1.09 CUSA03173 image offsets. Only the empty network table slot is
// changed; language/region globals and the region getter remain unchanged.
constexpr size_t Getter=0x1fb9190,Region=0x5128948,Table=0x5358440,Eu=0x49b7a24,Empty=0x49d85f0,End=Table+6*8;
constexpr std::array<unsigned char,6> Unlock={0xb8,5,0,0,0,0xc3};
constexpr char16_t EuUrl[]=u"https://ss4.scej-network.jp:20443/bb-eu/ss.info";
enum class Result{NotApplicable,Refused,AlreadyCorrect,Repaired};
template<class Writable>Result Repair(std::span<unsigned char> image,std::string_view serial,bool enabled,Writable writable){
 if(!enabled||serial!="CUSA03173")return Result::NotApplicable;
 const uintptr_t base=reinterpret_cast<uintptr_t>(image.data());
 if(!base||image.size()<End||base>UINTPTR_MAX-image.size())return Result::Refused;
 if(std::memcmp(image.data()+Getter,Unlock.data(),Unlock.size()))return Result::NotApplicable;
 uint32_t region{};std::memcpy(&region,image.data()+Region,4);
 if(region!=5)return Result::NotApplicable;
 uintptr_t eu{},current{};std::memcpy(&eu,image.data()+Table+2*8,8);std::memcpy(&current,image.data()+Table+5*8,8);
 if(eu!=base+Eu||std::memcmp(image.data()+Eu,EuUrl,sizeof(EuUrl)))return Result::Refused;
 if(current==eu)return Result::AlreadyCorrect;
 uint16_t empty{};std::memcpy(&empty,image.data()+Empty,2);
 if(current!=base+Empty||empty||!writable(base+Table+5*8,8))return Result::Refused;
 // Aligned, permanent image data. Applied before the first game HTTP request;
 // no new owner, string buffer, shader, cache entry or file is introduced.
 std::memcpy(image.data()+Table+5*8,&eu,8);
 return Result::Repaired;
}
}
