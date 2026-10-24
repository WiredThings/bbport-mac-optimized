// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "shader_recompiler/ir/passes/srt.h"
namespace Xbyak {class CodeGenerator;}
namespace Shader {
// Bounded, branch-free SRT programs only. Unsupported input emits nothing.
// The original code, program pointer and serialization bytes remain unchanged.
PFN_SrtWalker BuildSrtWatchClone(Xbyak::CodeGenerator&,const u8*,size_t,
    u64 watch,u64 bytes,void (*observer)(u64,u64),u32& encoded_reads,size_t capacity);
}
