// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bb_guest_callback.h"
#include <string>
namespace BbNet {
using GuestString=std::basic_string<char,std::char_traits<char>,LowAllocator<char>>;
inline const char* ExposeGuestString(GuestString& value) {
 if(value.empty())return nullptr;
 // Even short strings must leave native-object SSO storage before guest exposure.
 // Existing heap-backed contents retain their allocation; no second header/body copy.
 value.reserve(64);
 return value.c_str();
}
}
