// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef BB_ARM_RUNTIME_EXIT_CODES_H
#define BB_ARM_RUNTIME_EXIT_CODES_H
#include "runtime_restart_protocol.h"
enum {
 BB_ARM_EXIT_HLE_FAULT=74,
 BB_ARM_EXIT_NETWORK_INIT=77,
 BB_ARM_EXIT_RESTART_REJECTED=78,
 BB_ARM_EXIT_HOST_PROTECTION=79,
 BB_ARM_EXIT_UNSUPPORTED_IMPORT=80,
 BB_ARM_EXIT_BEFORE_GPU_SUBMIT=81,
 BB_ARM_EXIT_RESERVED_GUEST_STATUS=82,
 BB_ARM_EXIT_RESERVED_BACKEND_STATUS=83,
 BB_ARM_EXIT_LEGACY_RESTART_REQUEST=85,
 BB_ARM_EXIT_WATCHDOG=124,
 BB_ARM_EXIT_TSO_ADMISSION=190
};
/* The shell observes the low byte, including negative/oversized guest statuses.
 * Guest exit is never a launcher restart request. Only runtime_restart may emit
 * the original reserved statuses after reaping its owned backend. */
static inline int bb_arm_guest_exit_status(int raw) {
 unsigned status=(unsigned)raw&255u;
 return status==BB_RESTART_EXIT||status==BB_QUICKLOAD_EXIT ? BB_ARM_EXIT_RESERVED_GUEST_STATUS : raw;
}
static inline int bb_arm_backend_exit_status(int raw) {
 unsigned status=(unsigned)raw&255u;
 return status==BB_RESTART_EXIT||status==BB_QUICKLOAD_EXIT ? BB_ARM_EXIT_RESERVED_BACKEND_STATUS : raw;
}
#endif
