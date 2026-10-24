// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <CommonCrypto/CommonDigest.h>
#include <array>
#include <span>
#include <cstdint>
namespace BbArmHooks {
inline bool MatchCodeDigest(std::span<const uint8_t> code,const std::array<uint8_t,32>& expected){
 if(code.size()>UINT32_MAX)return false;std::array<uint8_t,32> actual{};
 CC_SHA256(code.data(),CC_LONG(code.size()),actual.data());return actual==expected;
}
// Fixed 1.09 leaf body: three balanced returns, no external calls. A digest
// guard avoids embedding the original game function in the source tree.
constexpr uint64_t PlainLeafOffset=0x2085730,PlainLeafSize=0x164;
constexpr std::array<uint8_t,32> PlainLeafDigest={0xad,0x8d,0x7a,0x19,0xe3,0x5b,0xbd,0xa6,0xa4,0xd0,0xba,0xc9,0xd9,0xc1,0xf,0xa,0x8f,0xd7,0xb1,0xd8,0x30,0x5a,0x3a,0x5,0x66,0x54,0x1f,0x5f,0xa8,0xf,0xf4,0xaa};
}
