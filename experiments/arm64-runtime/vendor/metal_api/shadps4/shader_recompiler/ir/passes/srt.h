// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <boost/container/set.hpp>
#include <boost/container/small_vector.hpp>
#include "common/types.h"

namespace Serialization {
struct Archive;
}

namespace Shader {

using PFN_SrtWalker = void PS4_SYSV_ABI (*)(const u32* /*user_data*/, u32* /*flat_dst*/);
PFN_SrtWalker RegisterWalkerCode(const u8* ptr, size_t size);
struct PersistentSrtInfo;
#if defined(__aarch64__)
void RunSrtWalker(const PersistentSrtInfo&,const u32*,u32*);
extern bool (*srt_guest_readable)(u64,u64);
extern u64 (*srt_guest_translate)(u64);
const u32* RegisterSrtBytecode(const u32*,size_t);
#endif
// Opt-in CPU-read diagnostics; never replaces the serialized original walker.
bool TryRunWatchedSrt(const PersistentSrtInfo&, const u32*, u32*, u64 shader, u32 stage);
bool IsSrtWatchCode(const void* code);
void EnsureSrtAccessHandler();

struct PersistentSrtInfo {
    // Special case when fetch shader uses step rates.
    struct SrtSharpReservation {
        u32 sgpr_base;
        u32 dword_offset;
        u32 num_dwords;
    };

    PFN_SrtWalker walker_func{};
    size_t walker_func_size{};
    u32 flattened_bufsize_dw = 16; // NumUserDataRegs

    void Serialize(Serialization::Archive& ar) const;
    bool Deserialize(Serialization::Archive& ar);
};

} // namespace Shader
