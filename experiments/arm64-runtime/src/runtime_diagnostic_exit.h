// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "runtime_exit_codes.h"
namespace BbArmExit {
inline constexpr int UnsupportedImport=BB_ARM_EXIT_UNSUPPORTED_IMPORT;
inline constexpr int BeforeGpuSubmit=BB_ARM_EXIT_BEFORE_GPU_SUBMIT;
static_assert(UnsupportedImport!=BB_RESTART_EXIT&&UnsupportedImport!=BB_QUICKLOAD_EXIT);
static_assert(BeforeGpuSubmit!=BB_RESTART_EXIT&&BeforeGpuSubmit!=BB_QUICKLOAD_EXIT);
static_assert(UnsupportedImport!=BB_ARM_EXIT_NETWORK_INIT&&BeforeGpuSubmit!=BB_ARM_EXIT_HOST_PROTECTION);
}
