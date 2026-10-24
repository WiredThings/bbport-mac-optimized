// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <vector>
#include "shader_recompiler/frontend/fetch_shader.h"
#include "video_core/renderer_metal/mtl_pso_cache.h"
namespace Metal {
// Metal only needs locations and instance stepping here. Formats are already in the
// pipeline key; actual draw buffers come from packet_vsharps. Do not re-read a guest
// V# table after shader compilation just to discard its format/address/stride.
inline std::vector<VertexAttribute> VertexMetadataFromFetch(const Shader::Gcn::FetchShaderData* fetch,
                                                   u32 step0, u32 step1) {
    std::vector<VertexAttribute> result;
    if (!fetch) return result;
    result.reserve(fetch->attributes.size());
    using Rate = Shader::Gcn::VertexAttribute::InstanceIdType;
    for (const auto& input : fetch->attributes) {
        const auto rate = input.GetStepRate();
        VertexAttribute attribute{.location=input.semantic, .per_instance=rate!=Rate::None};
        if (attribute.per_instance) {
            attribute.divisor = rate==Rate::OverStepRate0 ? step0 : rate==Rate::OverStepRate1 ? step1 : 1;
            // Preserve the current Metal caller's last matching divisor, including
            // duplicate semantics that appear later in the fetch shader.
            for (const auto& other : fetch->attributes) {
                if (other.semantic != input.semantic || other.GetStepRate() == Rate::None)
                    continue;
                const auto other_rate = other.GetStepRate();
                attribute.divisor = other_rate == Rate::OverStepRate0 ? step0
                                    : other_rate == Rate::OverStepRate1 ? step1 : 1;
            }
        }
        result.push_back(attribute);
    }
    return result;
}
}
