// SPDX-License-Identifier: GPL-2.0-or-later

// What the generated headers of the host shaders (build/include/video_core/host_shaders_msl/
// <name>_msl.h, written by gpu/tools/spirv_to_msl_header.cpp from the same GLSL as the Vulkan
// host shader headers; docs/METAL_BACKEND_PLAN.md D11) are made of. Plain C++, no Metal types:
// a header holds the MSL text and the resource layout, the Metal side creates the library.
//
// Resource slots are the direct slots of mtl_shader.h (D5): push constants at buffer 0, buffers
// from 1 in (set, binding) order, textures and samplers from 0 in (set, binding) order, so a
// host kernel binds like a game shader. Keep `Kind` and the constants in step with
// Metal::ShaderBinding (tests/test_msl_host_shaders.mm static_asserts it).

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace Metal::HostShaders {

enum class Stage : uint8_t {
    Vertex,
    Fragment,
    Compute,
};

/// One resource of the module and where the MSL puts it (ShaderBinding of mtl_shader.h).
struct Binding {
    enum class Kind : uint8_t {
        Buffer,         ///< UBO or SSBO; msl_index is a buffer slot
        Texture,        ///< sampled image (separate, or the image half of a combined one)
        StorageTexture, ///< read/write image
        Sampler,        ///< separate sampler (or the sampler half of a combined image sampler)
        TexelBuffer,    ///< image with Dim Buffer (a texture slot)
        PushConstants,  ///< set and binding are ~0u, msl_index == 0
    };
    uint32_t set;
    uint32_t binding;
    Kind kind;
    uint32_t msl_index; ///< slot in the class of `kind` (buffer, texture or sampler table)
    uint32_t count;     ///< array size: occupies msl_index .. msl_index + count - 1
};

/// A SPIR-V specialization constant, which SPIRV-Cross turns into an MSL function constant with
/// [[function_constant(id)]] (set with MTLFunctionConstantValues, index == `id`). The MSL falls
/// back to `default_value` (the raw 32-bit pattern; bool: 0/1) when it is not set.
struct SpecConstant {
    enum class Type : uint8_t { Bool, U32, I32, F32 };
    uint32_t id;
    Type type;
    uint32_t default_value;
};

struct Shader {
    Stage stage;
    std::string_view msl;
    std::string_view entry; ///< MSL function name ("main" becomes "main0")
    std::span<const Binding> bindings; ///< push constants entry first, then by (set, binding, kind)
    std::span<const SpecConstant> spec_constants;
    uint32_t local_size[3];      ///< compute: threadsPerThreadgroup of a dispatch, else 0
    uint32_t push_constant_size; ///< bytes of the push constant block, 0 if none
    /// SPIRV-Cross' buffer of buffer sizes (OpArrayLength): a constant uint[] at this buffer
    /// slot, indexed by the MSL buffer index of each SSBO; ~0u when the shader does not need it.
    uint32_t buffer_size_buffer;
    uint32_t num_buffers; ///< buffer slots used, incl. push constants and the size buffer
    uint32_t num_textures;
    uint32_t num_samplers;
};

} // namespace Metal::HostShaders
