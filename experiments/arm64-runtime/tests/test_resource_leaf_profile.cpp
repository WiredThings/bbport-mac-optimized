// SPDX-License-Identifier: GPL-2.0-or-later
#include "resource_leaf_profile.h"
#include <cassert>
#include <cstdio>
using namespace BbArmHooks;
int main(){
 const std::array<uint8_t,3> code={'a','b','c'};
 const std::array<uint8_t,32> known={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
 assert(MatchCodeDigest(code,known));auto changed=code;changed[2]^=1;assert(!MatchCodeDigest(changed,known));
 auto other=known;other[0]^=1;assert(!MatchCodeDigest(code,other));assert(!MatchCodeDigest(std::span(code).first(2),known));assert(!MatchCodeDigest({},known));
 assert(PlainLeafSize==356);puts("RESOURCE leaf code fingerprint: known digest accepted; byte, digest, length and empty mismatches rejected");
}
