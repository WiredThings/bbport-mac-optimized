// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace Common::BdaTable {
// Shader ABI: guest addresses stay 40-bit and pages keep their existing granularity.
inline constexpr unsigned AddressBits=40, LeafBits=12;
inline constexpr std::uint64_t LeafEntries=1ull<<LeafBits;
constexpr std::uint64_t RootEntries(unsigned page_shift) {
    return 1ull<<(AddressBits-page_shift-LeafBits);
}
constexpr std::uint64_t Pages(unsigned page_shift) {return 1ull<<(AddressBits-page_shift);}
} // namespace Common::BdaTable
