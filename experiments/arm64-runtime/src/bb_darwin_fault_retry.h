// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
// Only data translation/permission faults can be stale GPU watch delivery.
// Never treat instruction fetch, alignment or external abort as such a race.
static inline int bb_arm_is_data_protection_fault(uint32_t esr) {
 const unsigned ec=esr>>26,dfsc=esr&63;
 return (ec==0x24 || ec==0x25) && ((dfsc>=4&&dfsc<=7)||(dfsc>=12&&dfsc<=15));
}
