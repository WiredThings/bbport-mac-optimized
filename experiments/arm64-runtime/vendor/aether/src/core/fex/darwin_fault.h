// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace AetherPS4::Fex {
// Darwin reports BUS_ADRALN for ordinary page permission faults as well.
// ESR Data Fault Status Code distinguishes them without touching guest pages.
inline bool IsDarwinDataAlignmentEsr(std::uint32_t esr) noexcept {
 const auto ec=(esr>>26)&0x3f;
 return (ec==0x24||ec==0x25)&&(esr&0x3f)==0x21;
}
}
